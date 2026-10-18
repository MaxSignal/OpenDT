// D2 battle (spec 24): flow, screen, command input, AI, actions, end of battle.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/text.h"
#include "d2/battle.h"
#include "d2/game.h"
#include "d2/menus.h"

namespace d2 {
namespace {

using namespace bt;

std::string U(const char* utf8) { return core::utf8ToSjis(utf8); }
std::string num(int64_t v) { return std::to_string(v); }

enum RoundResult { kContinue, kVictory, kDefeat };

class Battle {
 public:
  Battle(Game& g) : g(g) {}

  int run(int group, int gameOverMode);

 private:
  Game& g;
  Battler s[10];
  int partyN = 0;
  int enemyN = 0;
  bool autoMode = false;
  bool effect = false;
  int lastTarget = -1;
  std::vector<uint8_t> bgCopy = std::vector<uint8_t>(size_t(640) * 400, 0);
  int nameRows = 0;
  int nameRowSlot[5] = {};

  // ---------------------------------------------------------------- text
  std::string word(int off) { return g.db.wordAt(off); }
  std::string statusName(int st) { return word(0xC5 + 9 * st); }
  std::string statusMsg(int st, int kind) { return word(0x104 + 0x84 * st + 0x21 * kind); }

  void msgLine(int n, const std::string& t) {
    g.scr.fillRect(104, 16 + 24 * n, 432, 16, 0);
    g.drawText(104, 16 + 24 * n, t, 15);
    if (g.opt.verbose) g.host.log("[msg" + std::to_string(n) + "] " + core::sjisToUtf8(t));
  }
  void clearLine(int n) { g.scr.fillRect(104, 16 + 24 * n, 432, 16, 0); }
  void clearMsg() {
    for (int n = 0; n < 3; ++n) clearLine(n);
  }
  void msgSingle(const std::string& t) {
    clearMsg();
    msgLine(0, t);
  }

  // ---------------------------------------------------------------- timing
  void units(int n) { g.waitUnits(n); }
  void waitUntil(uint32_t k) {
    while (g.waitCounter < k) g.vsync();
  }
  // Battle key wait (spec 24 §1.6). No STOP check: the event-running flag is 0 in battle (24 §3.1 v1.1).
  void keyWaitB() {
    if (g.cur.fast) {
      units(3);
      return;
    }
    bool released = false;
    for (;;) {
      g.recordLatch();
      g.clearKeys();
      g.waitMs(2);
      const int d = g.readDirection();
      if (d == 0)
        released = true;
      else if (released)
        break;
      g.rng.next();
    }
    while (g.anyHeld()) g.vsync();
  }

  // ---------------------------------------------------------------- palette
  void setPal4(const uint8_t p[16][3]) {
    for (int i = 0; i < 16; ++i)
      for (int k = 0; k < 3; ++k) g.curPal[i][k] = uint8_t((p[i][k] & 15) * 17);
  }

  // ---------------------------------------------------------------- selection (spec 24 §6.5)
  void cursor(int x, int y, int c) { g.drawCursor(x, y, c); }
  int selectB(int x, int y, int n, int mode) {
    if (n <= 0) return -1;
    g.clearKeys();
    int pos = 0, t = 0;
    for (;;) {
      const int cx = x + 16, cy = y + 16 + 24 * pos;
      if (t < 4)
        cursor(cx, cy, 15);
      else if (t >= 21 && t <= 23)
        cursor(cx, cy, 0);
      g.vsync();
      if (++t > 30) t = 0;
      const int k = g.popKey();
      if (k == kKeyNone) continue;
      if (k == kKeyEsc) {
        if (mode != 0) {
          g.sfx(3);
          cursor(cx, cy, 0);
          return -1;
        }
      } else if (isOkKey(k)) {
        g.sfx(2);
        cursor(cx, cy, mode == 2 ? 0 : 15);
        return pos;
      } else if (isUpKey(k) || isDownKey(k)) {
        g.sfx(1);
        cursor(cx, cy, 0);
        if (isUpKey(k))
          pos = pos == 0 ? n - 1 : pos - 1;
        else
          pos = pos == n - 1 ? 0 : pos + 1;
        t = 0;
      } else if (isLeftKey(k) || isRightKey(k)) {
        g.sfx(1);
      }
    }
  }


  // ---------------------------------------------------------------- windows (spec 24 §6)
  void partyRow(int i, bool highlight) {
    if (i < 0 || i >= partyN || !s[i].used) return;  // empty slot: no row (24 §19.17)
    const Battler& b = s[i];
    const int y = 256 + 24 * i;
    g.scr.fillRect(296, y - 2, 336, 20, highlight ? 15 : 0);
    const int col = highlight ? 0 : 15;
    g.drawText(312, y, b.name, col);
    std::string st;
    if (!b.alive) {
      st = statusName(6);
    } else {
      for (int k = 5; k >= 0; --k)
        if (b.status[k]) {
          st = statusName(k);
          break;
        }
      if (st.empty()) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%2d", b.level);
        st = "\x82\x6B" + std::string(buf);
      }
    }
    g.drawText(448, y, st, col);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%4lld/%4lld%5lld", (long long)b.hp, (long long)b.maxHp, (long long)b.mp);
    g.drawText(512, y, buf, col);
  }
  void updateRow(int slot) {
    if (slot >= 0 && slot < 5) partyRow(slot, false);
  }
  void partyWindow() {
    g.drawWindow(288, 240, 352, 144);
    for (int i = 0; i < partyN; ++i) partyRow(i, false);
  }
  void enemyNames() {
    g.drawWindow(120, 240, 168, 144);
    nameRows = 0;
    for (int e = 0; e < enemyN; ++e) {
      const Battler& b = s[5 + e];
      if (!b.used || !b.alive) continue;
      g.drawText(144, 256 + 24 * nameRows, b.name, 15);
      nameRowSlot[nameRows++] = 5 + e;
    }
  }
  void commandWindow() { g.drawWindow(0, 240, 120, 144); }

  // ---------------------------------------------------------------- enemy graphics (spec 24 §6.7, §6.8)
  int ew(const Battler& b) const { return b.sizeKind ? 160 : 80; }
  int eh(const Battler& b) const { return b.sizeKind ? 128 : 96; }
  // Normal / white / step-in drawing use the battler's size class dimensions (24 §12.3 v1.1).
  void drawNormal(const Battler& b, int dy = 0) {
    std::vector<uint8_t> mask, planes;
    classView(b.img, b.sizeKind, mask, planes);
    const int w = ew(b), h = eh(b);
    g.scr.maskedPlanes(mask.data(), planes.data(), (w / 8) * h, w, h, b.x & ~7, b.y + dy);
  }
  void drawWhite(const Battler& b) {
    std::vector<uint8_t> mask, planes;
    classView(b.img, b.sizeKind, mask, planes);
    const int w = ew(b), h = eh(b), bpr = w / 8, plane = bpr * h;
    const int x0 = b.x & ~7;
    for (int yy = 0; yy < h; ++yy)
      for (int bx = 0; bx < bpr; ++bx) {
        const size_t o = size_t(yy) * bpr + bx;
        const uint8_t v = mask[o] | planes[o] | planes[o + plane] | planes[o + 2 * size_t(plane)];
        for (int k = 0; k < 8; ++k)
          if (v & (0x80 >> k)) g.scr.pset(x0 + bx * 8 + k, b.y + yy, 15);
      }
  }
  void erase(const Battler& b) {
    const int x0 = b.x & ~7, w = ew(b), h = eh(b);
    for (int yy = 0; yy < h; ++yy) {
      const int y = b.y + yy;
      if (y < 0 || y >= 400) continue;
      for (int xx = 0; xx < w; ++xx) {
        const int x = x0 + xx;
        if (x < 0 || x >= 640) continue;
        g.scr.pix[y * 640 + x] = bgCopy[size_t(y) * 640 + x];
      }
    }
  }
  void hitBlink(const Battler& b) {
    g.waitCounter = 0;
    for (int k = 1; k <= 8; ++k) {
      waitUntil(uint32_t(2 * k));
      if (k & 1)
        drawWhite(b);
      else
        drawNormal(b);
    }
  }
  void vanish(const Battler& b) {
    g.waitCounter = 0;
    for (int k = 1; k <= 10; ++k) {
      waitUntil(uint32_t(4 * k));
      if (k & 1)
        drawNormal(b);
      else
        erase(b);
    }
  }
  void stepIn(const Battler& b) {
    g.waitCounter = 0;
    waitUntil(1);
    erase(b);
    drawNormal(b, 8);
    waitUntil(6);
    erase(b);
    drawNormal(b);
    const int h = b.imgKind ? 128 : 96, w = b.imgKind ? 160 : 80;
    g.scr.fillRect(b.x & ~7, b.y + h, w, 8, 14);
  }
  void shakeScreen() {
    for (int i = 0; i < 3; ++i) {
      g.waitFrames(2);
      g.scr.hOffset = 1;
      g.waitFrames(2);
      g.scr.hOffset = 0;
    }
  }
  void magicEffect(const Magic& mg) {
    g.sfx(mg.sfx);
    if (!mg.flash) {
      units(3);
      return;
    }
    uint8_t save[16][3];
    std::memcpy(save, g.curPal, sizeof(save));
    for (int n = 0; n < 10; ++n) {
      for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k) g.curPal[i][k] = uint8_t((mg.flashRgb[k] & 15) * 17);
      g.waitFrames(2);
      g.applyPalette();
      std::memcpy(g.curPal, save, sizeof(save));
      g.waitFrames(1);
      g.applyPalette();
    }
  }
  bool loadEgd(Battler& b, int enemyNo) {
    const Enemy& e = g.db.enemies[enemyNo];
    const std::string rel = e.size ? numberedName("EGD_ENL/ENL", e.image, ".EGD") : numberedName("EGD_ENS/ENS", e.image, ".EGD");
    EnemyImage img;
    if (loadEnemyImage(g.root, rel, img)) {
      b.img = std::move(img);
      b.imgKind = b.img.sizeKind;
      setPal4(b.img.pal);
      return true;
    }
    b.img = EnemyImage();
    b.imgKind = 0;
    return false;
  }

  // ---------------------------------------------------------------- state helpers
  void faint(Battler& b) {
    b.hp = 0;
    b.alive = false;
    std::memset(b.status, 0, 6);
    for (int k = 0; k < 4; ++k) b.cur[k] = b.base[k];
    b.charge = 0;
  }
  int slotOf(const Battler& b) const { return int(&b - s); }
  bool partyWiped() const {
    for (int i = 0; i < partyN; ++i)
      if (s[i].alive && !s[i].status[5]) return false;
    return true;
  }
  bool enemyWiped() {
    bool leaderDown = false;
    for (int e = 0; e < enemyN; ++e)
      if (s[5 + e].used && !s[5 + e].alive && s[5 + e].leader) leaderDown = true;
    if (leaderDown) {
      for (int e = 0; e < enemyN; ++e) {
        Battler& b = s[5 + e];
        if (b.used && b.alive) {
          b.alive = false;
          b.hp = 0;
          vanish(b);
        }
      }
      return true;
    }
    for (int e = 0; e < enemyN; ++e)
      if (s[5 + e].used && s[5 + e].alive) return false;
    return true;
  }
  int firstAliveEnemy() const {
    for (int e = 0; e < enemyN; ++e)
      if (s[5 + e].used && s[5 + e].alive) return 5 + e;
    return -1;
  }
  int partyLottery() {
    int list[5], n = 0;
    for (int i = 0; i < partyN; ++i)
      if (s[i].alive) list[n++] = i;
    if (n == 0) return -1;
    if (n < 3) return list[g.rng.next() % n];
    const int r = g.rng.next() % (4 * n + 2);
    return r < 2 ? list[r] : list[(r - 2) / 4];
  }
  int enemyLottery() {
    int list[5], n = 0;
    for (int e = 0; e < enemyN; ++e)
      if (s[5 + e].used && s[5 + e].alive) list[n++] = 5 + e;
    if (n == 0) return -1;
    return list[g.rng.next() % n];
  }
  bool cannotCast(const Battler& b, const Magic& mg) const {
    return b.mp < mg.mpCost || (mg.spiCoef != 0 && b.status[2]) || mg.target >= 4;
  }

  // ---------------------------------------------------------------- setup / teardown
  void transition();
  void prepare(int group);
  void writeBack();
  void cleanup();
  void victory();
  bool partyEscape();

  // ---------------------------------------------------------------- turn
  void commandInput();
  int commandMenu();
  int enemyTarget();
  int partyTarget();
  void enemyTurnStart();
  void enemyDecide(Battler& b);
  int bestSelfTarget(const Battler& b, const Magic& mg, int64_t* best);
  int bestPartyTarget(const Magic& mg, int64_t* best);
  void confusedDecide(Battler& b);
  void autoDecide(Battler& b);
  RoundResult round();

  // ---------------------------------------------------------------- actions
  void execute(Battler& b, RoundResult& rr);
  void physicalAttack(Battler& a, int target);
  void applyHp(Battler& c, Battler& t, int64_t dmg, DmgMode mode, int failKind);
  void transformCheck(Battler& t);
  void mpDamage(Battler& c, Battler& t, int64_t v, bool absorb);
  void healHp(Battler& t, int64_t v);
  void healMp(Battler& t, int64_t v);
  void inflict(Battler& t, int m, int st);
  void cure(Battler& t, int64_t amount, int st);
  void lower(Battler& t, int64_t v, int k);
  void raise(Battler& t, int64_t v, int k);
  void perTargetFail(Battler& t, const Magic& mg);
  void cast(Battler& c, int magicNo, int target);
  void applyToTargets(Battler& c, const Magic& mg, int target, bool reversed, bool fromItem);
  void attackType(Battler& c, const Magic& mg, int target);
  void supportType(Battler& c, const Magic& mg, int target);
  bool tryCounter(Battler& attacker, Battler& t, int k);
  void magicCounter(Battler& c, const Magic& mg, int lastT);
  void useItem(Battler& u, int item, int target);
  void healItem(Battler& t, const Item& it);
};

// =============================================================================================
// start / end (spec 24 §4, §5.3, §15)
void Battle::transition() {
  g.vsync();
  g.setTone(180);
  g.waitFrames(4);
  g.setTone(120);
  g.waitFrames(4);
  g.setTone(180);
  g.waitFrames(4);
  g.setTone(100);
  for (int i = 0; i < 24; ++i) {
    g.vsync();
    for (int j = 0; j < 16; j += 2) {
      g.scr.fillRect(0, i * 16 + j, 640, 1, 14);
      g.scr.fillRect(0, 383 - i * 16 - j, 640, 1, 14);
    }
  }
  g.vsync();
  g.setTone(0);
  g.scr.hOffset = 0;
  g.scr.clear(14);
  std::memcpy(bgCopy.data(), g.scr.pix, bgCopy.size());
}

void Battle::prepare(int group) {
  const EnemyGroup& grp = g.db.groups[group & 0xFF];
  int ids[5], n = 0;
  for (int i = 0; i < 5; ++i)
    if (grp.enemies[i] != 0xFF) ids[n++] = grp.enemies[i];
  transition();
  // background (24 §4.2 step 3). loadBattleBg rejects files under 41008 bytes: then nothing is
  // drawn, the palette is unchanged and the colour-14 fill of the transition remains.
  {
    char rel[32];
    std::snprintf(rel, sizeof(rel), "EBD_BKG/BKG%03u.EBD", unsigned(g.st.battleBg));
    BattleBg bg;
    if (loadBattleBg(g.root, rel, bg)) {
      setPal4(bg.pal);
      std::memcpy(g.scr.pix + 104 * 640, bg.px.data(), size_t(640) * 128);
    }
  }
  std::memcpy(bgCopy.data(), g.scr.pix, bgCopy.size());
  for (auto& b : s) b = Battler();
  partyN = std::min<int>(g.st.partyCount, 5);
  for (int i = 0; i < partyN; ++i) {
    const int c = g.st.party[i];
    if (c > 11) continue;
    initPartyBattler(s[i], g.members[c], c, g.db.items);
  }
  enemyN = n;
  for (int e = 0; e < n; ++e) initEnemyBattler(s[5 + e], g.db.enemies[ids[e]], ids[e]);
  for (int e = 0; e < n; ++e) {
    Battler& b = s[5 + e];
    b.sizeKind = loadEgd(b, ids[e]) ? b.imgKind : 0;
  }
  auto total = [&]() {
    int t = 0;
    for (int e = 0; e < enemyN; ++e) t += s[5 + e].sizeKind ? 168 : 104;
    return t;
  };
  while (enemyN > 0 && total() - 24 >= 601) {
    s[5 + enemyN - 1] = Battler();
    --enemyN;
  }
  {
    const int h = -((total() - 24) / 2);
    int x = h + 320;
    if (x % 8 != 0) x = h + 316;
    for (int e = 0; e < enemyN; ++e) {
      Battler& b = s[5 + e];
      if (b.sizeKind) {
        b.x = x - 8;
        b.y = 104;
        x += 168;
      } else {
        b.x = x;
        b.y = 136;
        x += 104;
      }
    }
  }
  g.drawWindow(80, 0, 480, 96);
  commandWindow();
  enemyNames();
  partyWindow();
  msgLine(0, word(0x4A0));
  for (int e = 0; e < enemyN; ++e) drawNormal(s[5 + e]);
  g.menuMask();
  g.vsync();
  g.setTone(100);
}

void Battle::writeBack() {
  for (int i = 0; i < partyN; ++i) {
    const Battler& b = s[i];
    if (!b.used || b.member < 0) continue;
    Member& m = g.members[b.member];
    m.hp = int16_t(b.hp);
    m.mp = int16_t(b.mp);
    m.alive = b.alive ? 1 : 0;
    m.status[0] = b.status[0];
    m.status[5] = b.status[5];
  }
  g.updatePoison();
}

void Battle::cleanup() {
  g.vsync();
  g.setTone(0);
  g.scr.hOffset = 0;
  g.scr.clear(0);
  g.fieldMask();
  g.clearKeys();
}

void Battle::victory() {
  clearMsg();
  msgLine(0, word(0x636));
  int64_t exp = 0, gold = 0;
  int drop = -1;
  for (int e = 0; e < enemyN; ++e) {
    exp += s[5 + e].exp;
    gold += s[5 + e].gold;
  }
  for (int e = 0; e < enemyN; ++e) {
    if (drop >= 0) break;
    const Battler& b = s[5 + e];
    if (g.rng.next() % 100 < b.dropRate && b.dropItem != 0xFF) drop = b.dropItem;
  }
  int line = 0;
  if (exp > 0 || gold > 0 || drop >= 0) {
    keyWaitB();
    clearMsg();
    line = 0;
  }
  bool up[5] = {};
  if (exp > 0) {
    units(2);
    msgLine(line++, U("それぞれ") + num(exp) + U("の経験値を獲得！"));
    for (int i = 0; i < partyN; ++i) {
      if (s[i].member < 0) continue;
      Member& m = g.members[s[i].member];
      m.exp = uint32_t(std::min<int64_t>(int64_t(m.exp) + exp, 9999999));
      up[i] = g.levelUp(m);
    }
    units(2);
  }
  if (gold > 0) {
    units(2);
    msgLine(line++, U("お金を") + num(gold) + word(0x6F) + U("手に入れた！"));
    g.addGold(long(std::min<int64_t>(gold, 9999999)));
    units(2);
  }
  if (drop >= 0) {
    units(2);
    msgLine(line++, core::rawName(reinterpret_cast<const uint8_t*>(g.db.items[drop].name), 16) + U("を手に入れた！"));
    g.addItem(drop);
    units(2);
  }
  for (int i = 0; i < partyN; ++i) {
    if (!up[i]) continue;
    keyWaitB();
    clearMsg();
    msgLine(0, s[i].name + U("はレベル") + num(g.members[s[i].member].level) + U("になった！"));
  }
  keyWaitB();
}

bool Battle::partyEscape() {
  int rate = 100;
  for (int e = 0; e < enemyN; ++e) {
    const Battler& b = s[5 + e];
    if (b.used && b.alive && b.escapeRate < rate) rate = b.escapeRate;
  }
  if (g.testPlay && g.cur.fast) rate = 100;
  if (g.rng.next() % 100 < rate) {
    msgLine(0, word(0x4BD));
    writeBack();
    keyWaitB();
    return true;
  }
  msgLine(0, word(0x4DA));
  units(5);
  return false;
}

// =============================================================================================
// command input (spec 24 §8)
int Battle::commandMenu() {
  g.vsync();
  commandWindow();
  for (int i = 0; i < 4; ++i) g.drawText(32, 256 + 24 * i, word(0x99 + 11 * i), 15);
  enemyNames();
  return selectB(0, 240, 4, 1);
}

int Battle::enemyTarget() {
  g.vsync();
  enemyNames();
  const int r = selectB(112, 240, nameRows, 1);
  return r < 0 ? -1 : nameRowSlot[r];
}

int Battle::partyTarget() {
  g.vsync();
  partyWindow();
  return selectB(280, 240, partyN, 2);
}

void Battle::commandInput() {
top:
  autoMode = false;
  g.vsync();
  commandWindow();
  for (int i = 0; i < 3; ++i) g.drawText(32, 256 + 24 * i, word(0x78 + 11 * i), 15);
  enemyNames();
  {
    const int sel = selectB(0, 240, 3, 0);
    if (sel == 1) {
      autoMode = true;
      return;
    }
    if (sel == 2) {
      for (int i = 0; i < partyN; ++i) {
        s[i].action = i == 0 ? kFlee : kNothing;
        s[i].defending = false;
      }
      return;
    }
  }
  bool back = false, redraw = false;
  int i = 0;
  while (i < partyN) {
    if (back) {
      if (i == 0) {
        back = false;
        goto top;
      }
      --i;
    }
    Battler& b = s[i];
    b.defending = false;
    if (!b.canAct()) {
      if (back) continue;
      b.action = kNothing;
      updateRow(i);
      ++i;
      continue;
    }
    back = false;
    if (redraw) {
      redraw = false;
      g.vsync();
      partyWindow();
    }
    for (;;) {
      partyRow(i, true);
      const int c = commandMenu();
      if (c < 0) {
        back = true;
        partyRow(i, false);
        break;
      }
      bool subCancel = false;
      if (c == 0) {
        b.action = kAttack;
        const int t = enemyTarget();
        if (t < 0)
          subCancel = true;
        else
          b.target = t;
      } else if (c == 1) {
        redraw = true;
        b.action = kMagic;
        const int m = magicListWindow(g, s[i].member, 144, 240);  // 23 §2.3 (shared)
        if (m < 0) {
          subCancel = true;
        } else {
          b.param = m;
          const int kind = g.db.magics[m].target;
          if (kind == 0 || kind == 2) {
            const int t = kind == 0 ? partyTarget() : enemyTarget();
            if (t < 0)
              subCancel = true;
            else
              b.target = t;
          }
        }
      } else if (c == 2) {
        redraw = true;
        b.action = kItem;
        const int a = itemListWindow(g, 144, 240);  // 23 §2.3 (shared)
        if (a < 0) {
          subCancel = true;
        } else {
          b.param = a;
          const Item& it = g.db.items[a];
          int need = -1;  // 0 party, 2 enemy
          if (it.kind == 6) need = 0;
          if (it.kind == 8) {
            const int mk = g.db.magics[it.magicNo()].target;
            if (mk == 0 || mk == 2) need = mk;
          }
          if (need >= 0) {
            const int t = need == 0 ? partyTarget() : enemyTarget();
            if (t < 0)
              subCancel = true;
            else
              b.target = t;
          }
        }
      } else {
        b.action = kDefend;
        b.defending = true;
      }
      if (subCancel) {
        if (redraw) {
          redraw = false;
          g.vsync();
          partyWindow();
        }
        continue;
      }
      partyRow(i, false);
      ++i;
      break;
    }
  }
  g.vsync();
  enemyNames();
  if (redraw) {
    g.vsync();
    partyWindow();
  }
}

// =============================================================================================
// enemy AI (spec 24 §9)
void Battle::enemyTurnStart() {
  for (int e = 0; e < enemyN; ++e) {
    Battler& b = s[5 + e];
    if (!b.used) continue;
    b.action = kAttack;
    b.defending = false;
    if (!b.alive) b.action = kDead;
    if (b.alive && b.listCount > 0 && b.charge == 0) {
      const int r = g.rng.next();
      if (b.listKind[r % b.listCount] == kDefend) {
        b.action = kDefend;
        b.defending = true;
      }
    }
  }
}

int Battle::bestSelfTarget(const Battler& b, const Magic& mg, int64_t* best) {
  int64_t mx = 0;
  int tgt = -1;
  for (int e = 0; e < enemyN; ++e) {
    const int v = evalSelf(mg, b, s[5 + e], &s[5 + e] == &b);
    if (v > mx) {
      mx = v;
      tgt = 5 + e;
    }
  }
  *best = mx;
  return tgt;
}

int Battle::bestPartyTarget(const Magic& mg, int64_t* best) {
  int64_t mx = 0;
  int tgt = -1;
  for (int p = 0; p < partyN; ++p) {
    const int v = evalVsParty(mg, s[p]);
    if (v > mx) {
      mx = v;
      tgt = p;
    }
  }
  *best = mx;
  return tgt;
}

void Battle::enemyDecide(Battler& b) {
  if (b.action == kDefend) return;
  int64_t cum[8], total = 0;
  for (int i = 0; i < b.listCount; ++i) {
    int64_t w = 0;
    switch (b.listKind[i]) {
      case kDefend: w = 1; break;
      case kMagic: {
        const Magic& mg = g.db.magics[b.listParam[i]];
        if (cannotCast(b, mg)) {
          w = 1;
        } else if (mg.target == 0) {
          int64_t mx;
          const int t = bestSelfTarget(b, mg, &mx);
          if (t >= 0) b.target = t;
          w = mx;
        } else if (mg.target == 1) {
          int64_t sum = 0;
          for (int e = 0; e < enemyN; ++e) sum += evalSelf(mg, b, s[5 + e], &s[5 + e] == &b);
          if (enemyN >= 2 && !b.flagA) sum /= enemyN;
          w = sum;
        } else if (mg.target == 2) {
          int64_t mx;
          const int t = bestPartyTarget(mg, &mx);
          if (t >= 0) b.target = t;
          w = mx;
        } else {
          int64_t sum = 0;
          for (int p = 0; p < partyN; ++p) sum += evalVsParty(mg, s[p]);
          if (partyN >= 2) sum /= partyN;
          w = sum;
        }
        break;
      }
      case kEscape: w = b.hp <= b.maxHp / 2 ? 100 : 50; break;
      case kCharge: w = b.charge == 0 ? 100 : 1; break;
      default: w = b.charge == 0 ? 100 : 3000; break;  // attack / double / poison
    }
    if (w > 0) w += g.rng.next() % 2;
    total += w;
    cum[i] = total;
  }
  if (total == 0) {
    b.action = kNothing;
    return;
  }
  const int64_t r = g.rng.next() % total;
  int sel = 0;
  for (int i = 0; i < b.listCount; ++i)
    if (cum[i] > r) {
      sel = i;
      break;
    }
  b.action = b.listKind[sel];
  b.param = b.listParam[sel];
  if (b.action == kAttack || b.action == kDouble || b.action == kPoisonAtk) {
    b.target = partyLottery();
  } else if (b.action == kMagic) {
    const Magic& mg = g.db.magics[b.param];
    int64_t mx;
    if (mg.target == 0) {
      const int t = bestSelfTarget(b, mg, &mx);
      if (t >= 0) b.target = t;
    } else if (mg.target == 2) {
      const int t = bestPartyTarget(mg, &mx);
      if (t >= 0) b.target = t;
    }
  }
}

void Battle::confusedDecide(Battler& b) {
  if (!b.enemy) {
    std::vector<int> list;
    for (int i = 0; i < 30; ++i)
      if (b.learned[i] != 0xFF) list.push_back(b.learned[i]);
    b.action = kAttack;
    if (!b.status[2] && !list.empty()) {
      if (g.rng.next() % 8 > 4) {
        b.action = kMagic;
        b.param = list[g.rng.next() % int(list.size())];
      }
    }
  } else {
    if (b.listCount == 0) {
      b.action = kNothing;
      return;
    }
    const int i = g.rng.next() % b.listCount;
    b.action = b.listKind[i];
    if (b.action == kMagic) b.param = b.listParam[i];
    if (b.action == kDefend) b.defending = true;
  }
  auto ownSide = [&]() { return b.enemy ? enemyLottery() : partyLottery(); };
  auto otherSide = [&]() { return b.enemy ? partyLottery() : enemyLottery(); };
  if (b.action == kAttack || b.action == kDouble || b.action == kPoisonAtk) {
    b.target = (g.rng.next() % 4 != 0) ? ownSide() : otherSide();
  } else if (b.action == kMagic) {
    const int mk = g.db.magics[b.param].target;
    if ((b.enemy && mk == 0) || (!b.enemy && mk == 2)) {
      b.target = (g.rng.next() % 4 != 0) ? partyLottery() : enemyLottery();
    } else if ((b.enemy && mk == 2) || (!b.enemy && mk == 0)) {
      b.target = (g.rng.next() % 4 != 0) ? enemyLottery() : partyLottery();
    }
  }
}

// =============================================================================================
// auto battle AI (spec 24 §13)
void Battle::autoDecide(Battler& b) {
  auto allyValue = [&](const Magic& mg, const Battler& p) -> int64_t {
    if (!p.alive) return mg.hasEffect(8) ? int64_t(int16_t(p.maxHp)) * 10 : 0;
    int64_t v = 0;
    if (mg.hasEffect(0)) {
      const int64_t h = healAmount(g.rng, mg, b);
      const int64_t inc = std::min(p.hp + h, p.maxHp) - p.hp;
      if (p.hp < p.maxHp / 4)
        v = inc * 10;
      else if (p.hp < p.maxHp / 2)
        v = inc * 5;
    }
    for (int st = 0; st < 6; ++st)
      if (p.status[st] && mg.hasEffect(2 + st)) v += (st + 1) * (int64_t(int16_t(p.maxHp)) / 4);
    if (mg.hasEffect(20)) v /= 2;
    return v;
  };
  auto enemyValue = [&](const Magic& mg, const Battler& e) -> int64_t {
    if (!e.alive || !mg.hasEffect(0)) return 0;
    bool el[6];
    for (int k = 0; k < 6; ++k) el[k] = mg.hasEffect(14 + k);
    const int m = elementMultiplier(el, e.resist);
    int64_t d = magicDamage(g.rng, mg, b, e, m);
    if (e.hp < d) d += d / 5;
    if (mg.hasEffect(20)) d /= 2;
    return d;
  };
  auto bestAttack = [&]() -> int64_t {
    int64_t mx = 0;
    for (int e = 0; e < enemyN; ++e) {
      const int64_t v = attackValue(b, s[5 + e]);
      if (v > mx) {
        mx = v;
        b.target = 5 + e;
      }
    }
    return mx;
  };
  auto magicValue = [&](const Magic& mg) -> int64_t {
    if (cannotCast(b, mg)) return 0;
    switch (mg.target) {
      case 0: {
        int64_t mx = 0;
        for (int p = 0; p < partyN; ++p) {
          const int64_t v = allyValue(mg, s[p]);
          if (v > mx) {
            mx = v;
            b.target = p;
          }
        }
        return mx;
      }
      case 1: {
        int64_t sum = 0;
        for (int p = 0; p < partyN; ++p) sum += allyValue(mg, s[p]);
        if (partyN >= 2) sum /= partyN;
        return sum;
      }
      case 2: {
        int64_t mx = 0;
        for (int e = 0; e < enemyN; ++e) {
          const int64_t v = enemyValue(mg, s[5 + e]);
          if (v > mx) {
            mx = v;
            b.target = 5 + e;
          }
        }
        return mx;
      }
      default: {
        int64_t sum = 0;
        for (int e = 0; e < enemyN; ++e) sum += enemyValue(mg, s[5 + e]);
        if (enemyN >= 2) sum /= enemyN;
        return sum;
      }
    }
  };
  int64_t val[31] = {};
  for (int i = 0; i < 30; ++i) {
    if (b.learned[i] == 0xFF) break;
    val[i] = magicValue(g.db.magics[b.learned[i]]);
  }
  val[30] = bestAttack();
  int64_t best = 0;
  int sel = -1;
  for (int i = 0; i < 31; ++i)
    if (val[i] >= best) {
      best = val[i];
      sel = i;
    }
  if (best <= 0 || sel < 0) {
    b.action = kNothing;
    return;
  }
  if (sel == 30) {
    b.action = kAttack;
    bestAttack();
    return;
  }
  b.action = kMagic;
  b.param = b.learned[sel];
  const Magic& mg = g.db.magics[b.param];
  if (mg.target == 0) {
    int64_t mx = 0;
    for (int p = 0; p < partyN; ++p) {
      const int64_t v = allyValue(mg, s[p]);
      if (v > mx) {
        mx = v;
        b.target = p;
      }
    }
  } else if (mg.target == 2) {
    int64_t mx = 0;
    for (int e = 0; e < enemyN; ++e) {
      const int64_t v = enemyValue(mg, s[5 + e]);
      if (v > mx) {
        mx = v;
        b.target = 5 + e;
      }
    }
  }
}

// =============================================================================================
// round (spec 24 §10, §11)
RoundResult Battle::round() {
  int order[10];
  actionOrder(g.rng, s, order);
  g.waitCounter = 0;
  g.waitFrames(6);
  for (int oi = 0; oi < 10; ++oi) {
    if (partyWiped()) return kDefeat;
    if (enemyWiped()) return kVictory;
    Battler& b = s[order[oi]];
    if (!b.alive) {
      if (b.used && b.revive && b.action == kDead) {
        if (++b.reviveCount >= 2) {
          b.reviveCount = 0;
          b.alive = true;
          b.hp = b.maxHp;
          drawNormal(b);
          msgSingle(b.name + word(0x588));
          units(4);
        }
      }
      continue;
    }
    if (b.enemy) stepIn(b);
    if (b.status[5]) {
      msgSingle(b.name + statusMsg(5, 2));
      units(4);
      continue;
    }
    if (b.status[4]) {
      if (g.rng.next() % 7 == 3) {
        b.status[4] = 0;
        updateRow(slotOf(b));
        msgSingle(b.name + statusMsg(4, 3));
      } else {
        msgSingle(b.name + statusMsg(4, 2));
      }
      units(4);
      continue;
    }
    if (b.status[3]) {
      if (g.rng.next() % 9 == 3) {
        b.status[3] = 0;
        updateRow(slotOf(b));
        msgSingle(b.name + statusMsg(3, 3));
        units(4);
        continue;
      }
      confusedDecide(b);
    }
    if (b.status[2]) {
      if (g.rng.next() % 15 == 3) {
        b.status[2] = 0;
        updateRow(slotOf(b));
        msgSingle(b.name + statusMsg(2, 3));
        units(4);
      }
    }
    if (b.status[1]) {
      if (g.rng.next() % 13 == 3) {
        b.status[1] = 0;
        updateRow(slotOf(b));
        msgSingle(b.name + statusMsg(1, 3));
        units(4);
      }
    }
    const int n = b.act3 ? 3 : (b.act2 ? 2 : 1);
    for (int k = 0; k < n; ++k) {
      b.interrupt = false;
      if (b.charge) --b.charge;
      if (!b.enemy && autoMode) {
        if (b.status[3])
          confusedDecide(b);
        else
          autoDecide(b);
      }
      if (b.enemy) {
        if (k >= 1) b.action = kAttack;
        if (b.status[3])
          confusedDecide(b);
        else
          enemyDecide(b);
      }
      RoundResult rr = kContinue;
      execute(b, rr);
      if (rr == kDefeat) return kDefeat;
      bool stop = false;
      if (b.status[4] || b.status[5] || !b.alive) stop = true;
      if (b.interrupt) {
        b.interrupt = false;
        stop = true;
      }
      if (partyWiped()) return kDefeat;
      if (enemyWiped()) return kVictory;
      if (stop) break;
    }
  }
  for (int i = 0; i < 10; ++i) {
    Battler& b = s[i];
    if (!b.used || !b.alive || !b.status[0] || b.hp <= 1) continue;
    int64_t d = (b.maxHp + g.rng.next() % 30) / 20;
    if (d == 0) d = 1;
    if (d < b.hp)
      b.hp -= d;
    else
      b.hp = 1;
    updateRow(i);
  }
  if (partyWiped()) return kDefeat;
  if (enemyWiped()) return kVictory;
  return kContinue;
}

// =============================================================================================
// actions (spec 24 §12)
void Battle::execute(Battler& b, RoundResult& rr) {
  switch (b.action) {
    case kAttack: physicalAttack(b, b.target); break;
    case kMagic: cast(b, b.param, b.target); break;
    case kDefend:
      clearMsg();
      msgLine(0, b.name + word(0x531));
      units(5);
      b.defending = true;
      break;
    case kEscape:
      clearMsg();
      msgLine(0, b.name + word(0x56B));
      vanish(b);
      units(4);
      b.exp = b.gold = 0;
      b.dropRate = 0;
      b.alive = false;
      b.hp = 0;
      b.revive = false;
      break;
    case kDouble:
      physicalAttack(b, b.target);
      if (partyWiped()) {
        rr = kDefeat;
        return;
      }
      physicalAttack(b, b.target);
      break;
    case kCharge:
      clearMsg();
      msgLine(0, b.name + word(0x54E));
      units(5);
      b.charge = 2;
      break;
    case kPoisonAtk:
      b.addEff[0] = true;
      b.addRate = 100;
      physicalAttack(b, b.target);
      b.addEff[0] = false;
      b.addRate = 0;
      break;
    case kItem: useItem(b, b.param, b.target); break;
    default: break;
  }
}

void Battle::physicalAttack(Battler& a, int target) {
  // spec 24 §19.7: no target -> nothing
  if (target < 0 || target > 9 || !s[target].used) {
    if (a.enemy) return;
    target = firstAliveEnemy();
    if (target < 0) return;
  }
  if (!s[target].alive) {
    if (a.enemy) return;
    target = firstAliveEnemy();
    if (target < 0) return;
  }
  Battler& t = s[target];
  int hit = a.hitRate;
  if (a.status[1]) hit /= 4;
  bool crit = false;
  int64_t dmg = 0;
  if (g.rng.next() % 100 < hit) {
    if (g.rng.next() % 100 < a.critRate) crit = true;
    const int64_t atk = a.cur[0], def = t.cur[1];
    dmg = crit ? atk * 2 - def / 3 : atk - def / 2;
    if (a.charge) {
      a.charge = 0;
      dmg += atk / 2;
    }
    const int m = elementMultiplier(a.atkElem, t.resist);
    dmg = applyMultiplier(dmg, m);
    if (m == 0)
      dmg = 0;
    else if (dmg < 2)
      dmg = g.rng.next() % 2;
    else
      dmg = variance(g.rng, dmg, 3);
    if (dmg <= 0) {
      dmg = 0;
      crit = false;
    }
  } else {
    a.charge = 0;
  }
  clearMsg();
  g.sfx(a.enemy ? 5 : 4);
  msgLine(0, a.name + word(0x4F7));
  units(3);
  if (crit) {
    msgLine(1, word(t.enemy ? 0x5FC : 0x619));
    units(4);
  }
  const int64_t raw = dmg;
  applyHp(a, t, dmg, kPhysical, -1);
  if (t.hp != 0 && raw != 0 && !(t.defending && raw == 1)) {
    for (int st = 0; st <= 6; ++st)
      if (a.addEff[st] && g.rng.next() % 100 < a.addRate) inflict(t, 100, st);
    if (!tryCounter(a, t, 0)) {
      for (int e = 0; e < 6; ++e)
        if (a.atkElem[e] && tryCounter(a, t, e + 1)) break;
    }
  }
}

void Battle::applyHp(Battler& c, Battler& t, int64_t dmg, DmgMode mode, int failKind) {
  if (t.defending) dmg /= 2;
  if (dmg > 9999) dmg = 9999;
  if (dmg == 0) {
    if (mode == kPhysical || failKind == 3) {
      g.sfx(7);
      msgLine(1, t.name + word(0x514));
      units(4);
      effect = true;
    } else if (failKind == 2) {
      msgLine(1, t.name + word(0x5DF));
      units(4);
      effect = true;
    }
    return;
  }
  if (mode != kPhysical) effect = true;
  std::string m;
  if (mode == kAbsorb) {
    if (dmg > t.hp) dmg = t.hp;
    m = t.enemy ? t.name + U("のＨＰを") + num(dmg) + U("奪った！") : t.name + U("はＨＰを") + num(dmg) + U("奪われた！");
    c.hp = std::min(c.hp + dmg, c.maxHp);
  } else {
    m = t.name + U("に") + num(dmg) + U("のダメージ！");
    g.sfx(6);
  }
  msgLine(1, m);
  if (t.enemy) {
    hitBlink(t);
    units(2);
  } else if (mode == kAbsorb) {
    units(4);
  } else {
    shakeScreen();
    units(2);
  }
  if (dmg < t.hp)
    t.hp -= dmg;
  else
    faint(t);
  updateRow(slotOf(c));
  updateRow(slotOf(t));
  if (t.hp == 0) {
    if (t.enemy) {
      msgLine(2, t.name + statusMsg(6, 1));
      vanish(t);
    } else {
      msgLine(2, t.name + statusMsg(6, 0));
      units(2);
    }
  } else if (dmg != 0 && t.status[4]) {
    if (g.rng.next() % 4 == 1) {
      t.status[4] = 0;
      updateRow(slotOf(t));
      msgLine(2, t.name + statusMsg(4, 3));
      units(2);
    }
  }
  transformCheck(t);
  units(2);
}

void Battle::transformCheck(Battler& t) {
  if (!t.enemy || !t.alive || t.transformTo == 0xFF || t.hp > t.transformHp) return;
  const int to = t.transformTo;
  const Enemy& ne = g.db.enemies[to];
  if (ne.image == t.image) {
    const int keepSize = t.sizeKind;
    initEnemyBattler(t, ne, to);
    t.sizeKind = keepSize;
  } else {
    vanish(t);
    initEnemyBattler(t, ne, to);
    loadEgd(t, to);  // palette changes only if the file is usable
    g.applyPalette();
    drawNormal(t);
  }
  t.interrupt = true;
}

void Battle::mpDamage(Battler& c, Battler& t, int64_t v, bool absorb) {
  if (v > t.mp) v = t.mp;
  if (v == 0) return;
  if (v > 9999) v = 9999;
  effect = true;
  std::string m;
  if (absorb) {
    m = t.enemy ? t.name + U("のＭＰを") + num(v) + U("奪った！") : t.name + U("はＭＰを") + num(v) + U("奪われた！");
    c.mp = std::min(c.mp + v, c.maxMp);
  } else {
    m = t.name + U("のＭＰが") + num(v) + U("減った！");
  }
  msgLine(1, m);
  if (t.enemy) {
    hitBlink(t);
    units(2);
  } else {
    units(4);
  }
  if (t.mp < 9999) t.mp -= v;
  updateRow(slotOf(c));
  updateRow(slotOf(t));
}

void Battle::healHp(Battler& t, int64_t v) {
  if (v == 0) return;
  if (v > 9999) v = 9999;
  effect = true;
  msgLine(1, v == 9999 ? t.name + U("のＨＰが全回復した！") : t.name + U("のＨＰが") + num(v) + U("回復した！"));
  units(4);
  t.hp = std::min(t.hp + v, t.maxHp);
  updateRow(slotOf(t));
}

void Battle::healMp(Battler& t, int64_t v) {
  if (v == 0) return;
  if (v > 999) v = 999;
  effect = true;
  msgLine(1, v == 999 ? t.name + U("のＭＰが全回復した！") : t.name + U("のＭＰが") + num(v) + U("回復した！"));
  units(4);
  t.mp = std::min(t.mp + v, t.maxMp);
  updateRow(slotOf(t));
}

void Battle::inflict(Battler& t, int m, int st) {
  const int rate = statusRate(m, t.resist[2 + st]);
  if (g.rng.next() % 100 >= rate) return;
  effect = true;
  const int kind = (st < 6 && t.status[st]) ? 2 : (t.enemy ? 1 : 0);
  msgLine(1, t.name + statusMsg(st, kind));
  if (st == 6)
    faint(t);
  else
    t.status[st] = 1;
  if (!t.enemy) {
    units(4);
    updateRow(slotOf(t));
  } else {
    hitBlink(t);
    if (st == 6) vanish(t);
    units(2);
  }
}

void Battle::cure(Battler& t, int64_t amount, int st) {
  if (st == 6 && t.alive) return;
  if (st < 6 && !t.status[st]) return;
  effect = true;
  if (st == 6) {
    t.alive = true;
    t.hp = std::min(amount, t.maxHp);
    if (t.hp <= 0) t.hp = 1;
    t.reviveCount = 0;
  } else {
    t.status[st] = 0;
  }
  if (st == 6 && t.enemy) {
    msgLine(1, t.name + word(0x588));
    drawNormal(t);
  } else {
    msgLine(1, t.name + statusMsg(st, 3));
  }
  units(4);
  updateRow(slotOf(t));
}

void Battle::lower(Battler& t, int64_t v, int k) {
  const int rate = lowerRate(t.resist[10 + k]);
  if (g.rng.next() % 100 >= rate) return;
  if (v > 999) v = 999;
  const int64_t old = t.cur[k];
  int64_t nv = old - v;
  if (nv < t.base[k] / 2) nv = t.base[k] / 2;
  t.cur[k] = int(nv);
  const int64_t d = old - nv;
  if (d > 0) {
    effect = true;
    msgLine(1, t.name + U("の") + word(0x16 + 11 * k) + U("が") + num(d) + U("下がった！"));
    units(4);
  }
}

void Battle::raise(Battler& t, int64_t v, int k) {
  if (v > 999) v = 999;
  const int64_t old = t.cur[k];
  int64_t nv = old + v;
  if (nv > int64_t(t.base[k]) * 2 + 10) nv = int64_t(t.base[k]) * 2 + 10;
  t.cur[k] = int(nv);
  const int64_t d = nv - old;
  if (d > 0) {
    effect = true;
    msgLine(1, t.name + U("の") + word(0x16 + 11 * k) + U("が") + num(d) + U("上がった！"));
    units(4);
  }
}

void Battle::perTargetFail(Battler& t, const Magic& mg) {
  if (mg.failKind == 2) {
    msgLine(1, t.name + word(0x5DF));
    units(4);
  } else if (mg.failKind == 3) {
    g.sfx(7);
    msgLine(1, t.name + word(0x514));
    units(4);
  }
}

void Battle::attackType(Battler& c, const Magic& mg, int target) {
  if (target < 0 || target > 9 || !s[target].used) return;  // §19.7
  if (!s[target].alive) {
    if (c.enemy) return;
    target = firstAliveEnemy();
    if (target < 0) return;
  }
  Battler& t = s[target];
  lastTarget = target;
  clearLine(1);
  clearLine(2);
  bool fail = g.rng.next() % 100 >= mg.success;
  // The blind re-check always draws, even when the success check already failed (24 §12.10.1 v1.1).
  if (c.status[1] && g.rng.next() % 10 < mg.atkCoef) fail = true;
  if (fail) {
    perTargetFail(t, mg);
    return;
  }
  bool el[6];
  for (int k = 0; k < 6; ++k) el[k] = mg.hasEffect(14 + k);
  const int m = elementMultiplier(el, t.resist);
  if (mg.hasEffect(0)) {
    int64_t d = resistHpMp(magicDamage(g.rng, mg, c, t, m), t.resist[0]);
    if (mg.hasEffect(21) && t.defending) d *= 2;
    applyHp(c, t, d, mg.hasEffect(9) ? kAbsorb : kMagical, mg.failKind);
    if (!t.alive) {
      if (mg.failKind >= 2) effect = false;
      return;
    }
  }
  if (mg.hasEffect(1)) {
    const int64_t d = resistHpMp(magicDamage(g.rng, mg, c, t, m), t.resist[0]);
    mpDamage(c, t, d, mg.hasEffect(9));
  }
  for (int st = 0; st <= 6; ++st)
    if (mg.hasEffect(2 + st)) inflict(t, m, st);
  for (int k = 0; k < 4; ++k)
    if (mg.hasEffect(10 + k)) {
      const int64_t v = magicDamage(g.rng, mg, c, t, m);
      lower(t, v, k);
    }
  if (!effect) perTargetFail(t, mg);
  if (mg.failKind >= 2) effect = false;
}

void Battle::supportType(Battler& c, const Magic& mg, int target) {
  if (target < 0 || target > 9 || !s[target].used) return;  // §19.7
  Battler& t = s[target];
  lastTarget = target;
  clearLine(1);
  clearLine(2);
  if (g.rng.next() % 100 >= mg.success) {
    perTargetFail(t, mg);
    return;
  }
  if (t.alive) {
    if (mg.hasEffect(0)) healHp(t, healAmount(g.rng, mg, c));
    for (int st = 0; st < 6; ++st)
      if (mg.hasEffect(2 + st)) {
        const int64_t v = healAmount(g.rng, mg, c);
        cure(t, v, st);
      }
    for (int k = 0; k < 4; ++k)
      if (mg.hasEffect(10 + k)) raise(t, healAmount(g.rng, mg, c), k);
  }
  if (mg.hasEffect(8)) {
    const int64_t v = healAmount(g.rng, mg, c);
    cure(t, v, 6);
  }
  if (!effect) perTargetFail(t, mg);
  if (mg.failKind >= 2) effect = false;
}

void Battle::applyToTargets(Battler& c, const Magic& mg, int target, bool rev, bool fromItem) {
  auto aliveEnemies = [&](auto fn) {
    for (int e = 0; e < enemyN; ++e)
      if (s[5 + e].used && s[5 + e].alive) fn(5 + e);
  };
  auto aliveParty = [&](auto fn) {
    for (int p = 0; p < partyN; ++p)
      if (s[p].alive) fn(p);
  };
  auto allParty = [&](auto fn) {
    for (int p = 0; p < partyN; ++p)
      if (s[p].used) fn(p);  // empty slots are never targets (24 §19.17)
  };
  auto allEnemies = [&](auto fn) {
    for (int e = 0; e < enemyN; ++e)
      if (s[5 + e].used) fn(5 + e);
  };
  auto sup = [&](int t) { supportType(c, mg, t); };
  auto atk = [&](int t) { attackType(c, mg, t); };
  if (fromItem) {
    switch (mg.target) {
      case 0: sup(target); break;
      case 1: allParty(sup); break;
      case 2: atk(target); break;
      case 3: aliveEnemies(atk); break;
      default: break;
    }
    return;
  }
  if (!c.enemy) {
    switch (mg.target) {
      case 0: sup(target); break;
      case 1:
        if (!rev) allParty(sup);
        else aliveEnemies(sup);
        break;
      case 2: atk(target); break;
      case 3:
        if (!rev) aliveEnemies(atk);
        else aliveParty(atk);
        break;
      default: break;
    }
  } else {
    switch (mg.target) {
      case 0: sup(target); break;
      case 1:
        if (!rev) allEnemies(sup);
        else allParty(sup);
        break;
      case 2: atk(target); break;
      case 3:
        if (!rev) aliveParty(atk);
        else aliveEnemies(atk);
        break;
      default: break;
    }
  }
}

void Battle::cast(Battler& c, int magicNo, int target) {
  const Magic& mg = g.db.magics[magicNo & 0xFF];
  effect = false;
  clearMsg();
  msgLine(0, c.name + core::rawName(reinterpret_cast<const uint8_t*>(mg.message), 39));
  magicEffect(mg);
  if (c.mp < mg.mpCost) {
    units(1);
    msgLine(1, word(0x0B) + U("が足りない！"));
    units(5);
    return;
  }
  if (mg.spiCoef != 0 && c.status[2]) {
    units(1);
    msgLine(1, U("しかし") + c.name + statusMsg(2, 2));
    units(5);
    return;
  }
  if (c.mp < 9999) c.mp -= mg.mpCost;
  updateRow(slotOf(c));
  bool rev = false;
  if ((mg.target == 1 || mg.target == 3) && c.status[3]) rev = g.rng.next() % 4 != 0;
  lastTarget = -1;
  applyToTargets(c, mg, target, rev, false);
  bool anyBit = false;
  for (int n = 0; n < 20; ++n)
    if (mg.hasEffect(n)) anyBit = true;
  if (!effect && mg.target <= 3 && !anyBit) {
    effect = true;
    units(2);
  }
  if (!effect && (mg.failKind == 0 || mg.failKind == 1)) {
    msgLine(1, word(0x4A0 + (mg.failKind + 9) * 0x1D));
    units(5);
  }
  if (mg.hasEffect(20)) {
    faint(c);
    if (!c.enemy)
      updateRow(slotOf(c));
    else
      vanish(c);
  }
  magicCounter(c, mg, lastTarget);
}

bool Battle::tryCounter(Battler& attacker, Battler& t, int k) {
  if (attacker.enemy || !t.enemy || !t.counter[k] || !t.alive || t.status[5] || t.status[4]) return false;
  const Magic& mg = g.db.magics[t.counterMagic[k]];
  if (t.mp < mg.mpCost) return false;
  if (mg.spiCoef != 0 && t.status[2]) return false;
  int target = -1;
  if (mg.target == 0) target = slotOf(t);
  if (mg.target == 2) target = slotOf(attacker);
  const int saveLast = lastTarget;
  const bool saveEffect = effect;
  cast(t, t.counterMagic[k], target);
  lastTarget = saveLast;
  effect = saveEffect;
  return true;
}

void Battle::magicCounter(Battler& c, const Magic& mg, int lastT) {
  if (c.enemy) return;
  if (mg.target == 2) {
    if (lastT < 0) return;
    for (int e = 0; e < 6; ++e)
      if (mg.hasEffect(14 + e) && tryCounter(c, s[lastT], e + 1)) break;
  } else if (mg.target == 3) {
    for (int en = 0; en < enemyN; ++en) {
      if (!s[5 + en].used) continue;
      for (int e = 0; e < 6; ++e)
        if (mg.hasEffect(14 + e) && tryCounter(c, s[5 + en], e + 1)) break;
    }
  }
}

void Battle::healItem(Battler& t, const Item& it) {
  if (t.alive) {
    if (!(it.cureBits() & 0x40) && it.healHp() != 0) healHp(t, uint16_t(it.healHp()));
    if (it.healMp() != 0) healMp(t, uint16_t(it.healMp()));
    for (int st = 0; st < 6; ++st)
      if (it.cureBits() & (1 << st)) cure(t, 0, st);
  }
  if (it.cureBits() & 0x40) cure(t, 0, 6);
}

void Battle::useItem(Battler& u, int a, int target) {
  const Item& it = g.db.items[a & 0xFF];
  const int kind = it.kind;
  if (kind >= 6 && kind <= 8) {
    for (int i = 0; i < 256; ++i) {
      if (g.inventory[i][1] == 0) return;
      if (g.inventory[i][0] == a) {
        if (it.consumable()) {
          if (--g.inventory[i][1] == 0) g.packInventory();
        }
        break;
      }
    }
  }
  effect = false;
  clearMsg();
  msgLine(0, u.name + U("は") + core::rawName(reinterpret_cast<const uint8_t*>(it.name), 16) + U("を使った！"));
  const Magic& mg = g.db.magics[it.magicNo()];
  if (kind == 8)
    magicEffect(mg);
  else
    units(3);
  if (kind == 8) {
    lastTarget = -1;
    applyToTargets(u, mg, target, false, true);
  } else if (kind == 6) {
    if (target >= 0 && target <= 9 && s[target].used) healItem(s[target], it);
  } else if (kind == 7) {
    for (int p = 0; p < partyN; ++p)
      if (s[p].used) healItem(s[p], it);
  }
  if (kind == 8) {
    if (!effect && (mg.failKind == 0 || mg.failKind == 1)) {
      msgLine(1, U("しかし何も起こらなかった！"));
      units(5);
    }
    if (mg.hasEffect(20)) {
      faint(u);
      updateRow(slotOf(u));
    }
    magicCounter(u, mg, lastTarget);
  } else if (!effect) {
    msgLine(1, U("しかし何も起こらなかった！"));
    units(5);
  }
}

// =============================================================================================
// main (spec 24 §4.1, §7)
int Battle::run(int group, int gameOverMode) {
  g.st.steps = 0;
  g.itemListPage = g.itemListPos = 0;
  for (int c = 0; c < 12; ++c) g.magicListPage[c] = g.magicListPos[c] = 0;
  prepare(group);
  units(4);
  autoMode = false;
  if (enemyWiped()) {
    writeBack();
    victory();
    cleanup();
    return 1;
  }
  for (;;) {
    clearMsg();
    bool autoCont = false;
    if (autoMode) {
      g.vsync();
      enemyNames();
      autoCont = true;
      for (int k = 0; k < 4; ++k) {
        units(1);
        if (g.cur.cancel) {
          autoMode = false;
          autoCont = false;
          break;
        }
      }
    }
    if (!autoCont) {
      bool any = false;
      for (int i = 0; i < partyN; ++i)
        if (s[i].canAct()) any = true;
      if (any) {
        commandInput();
      } else {
        for (int i = 0; i < partyN; ++i) {
          s[i].action = kNothing;
          s[i].defending = false;
        }
        g.vsync();
        enemyNames();
        units(4);
      }
      enemyTurnStart();
      g.vsync();
      commandWindow();
      if (autoMode) g.drawText(24, 256, word(0x83), 15);
    } else {
      enemyTurnStart();
    }
    clearMsg();
    if (partyN > 0 && s[0].action == kFlee) {
      g.drawText(24, 256, word(0x8E), 15);
      s[0].action = kNothing;
      if (partyEscape()) {
        cleanup();
        return 2;
      }
    } else if (!autoMode) {
      g.drawText(24, 256, word(0x78), 15);
    }
    const RoundResult r = round();
    if (r == kVictory) {
      writeBack();
      victory();
      cleanup();
      return 1;
    }
    if (r == kDefeat) {
      writeBack();
      clearMsg();
      msgLine(0, word(0x653));
      units(5);
      keyWaitB();
      if (gameOverMode) {
        g.blackOut(2);
        throw ReturnToTitle();
      }
      cleanup();
      return 3;
    }
  }
}

}  // namespace

int battle(Game& g, int group, int gameOverMode) {
  Battle b(g);
  try {
    const int r = b.run(group, gameOverMode);
    g.host.log("[battle " + std::to_string(group) + " result " + std::to_string(r) + "]");
    return r;
  } catch (ReturnToTitle&) {
    g.host.log("[battle " + std::to_string(group) + " game over]");
    throw;
  }
}

}  // namespace d2
