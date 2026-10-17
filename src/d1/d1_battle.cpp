// D1 battle (spec 12).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/text.h"
#include "d1/d1_game.h"

namespace d1 {

namespace {
const int kTypeTab[32] = {1, 1, 1, 0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 4, 4, 4, 2, 2, 2, 4, 4, 4};
bool isAllMagic(int m) { return m == 3 || m == 4 || m == 23 || m == 24 || m == 25 || m == 29 || m == 30 || m == 31; }
const int kAbort = 0x7FFF0000;  // "中断コード"
const int kNoCaster = -1, kItemCaster = -2;
const int kFixed = 100;  // group kind: fixed damage (self-destruct)

std::string fmt(const char* f, long v) {
  char b[64];
  std::snprintf(b, sizeof b, f, v);
  return b;
}
}  // namespace

class Battle {
 public:
  Battle(Game& g) : g(g) {}
  int run(int group, bool eventBattle);

 private:
  Game& g;
  BEnemy en[5];
  int k = 0;
  int bAtk[4] = {}, bDef[4] = {}, bAgi[4] = {}, bLuck[4] = {};
  bool hidden[9] = {}, guard[9] = {}, canAct[9] = {};
  int curAct = 0, curTarget = 0;
  int inCmd[4] = {0, 0, 0, 0}, inTarget[4] = {0, 0, 0, 0};
  int order[9];
  int result = 1;

  // ---- combatant helpers
  bool valid(int s) const { return g.validSlot(s); }
  BEnemy& E(int a) { return en[a - 4]; }
  bool enemyExists(int a) const { return a - 4 < k; }
  bool alive(int a) {
    if (a < 0 || a > 8) return false;
    if (a < 4) return valid(a) && g.heroes[g.party[a]].hp > 0;
    if (!enemyExists(a)) return false;
    return E(a).maxHp > 0 && E(a).hp > 0;
  }
  int& status(int a) {
    static int dummy;
    if (a < 4) return valid(a) ? g.heroes[g.party[a]].status : (dummy = 0);
    return enemyExists(a) ? E(a).status : (dummy = 0);
  }
  int& hp(int a) {
    static int dummy;
    if (a < 4) return valid(a) ? g.heroes[g.party[a]].hp : (dummy = 0);
    return enemyExists(a) ? E(a).hp : (dummy = 0);
  }
  int recMax(int a) {
    if (a < 4) return valid(a) ? g.heroes[g.party[a]].recMaxHp : 0;
    return enemyExists(a) ? E(a).maxHp : 0;
  }
  std::string name(int a) {
    if (a < 0 || a > 8) return std::string();
    if (a < 4) return valid(a) ? g.heroName(g.party[a]) : std::string();
    return enemyExists(a) ? core::rawName(E(a).name, 17) : std::string();
  }
  int flags(int a) { return (a >= 4 && enemyExists(a)) ? E(a).flags : 0; }
  void put(const std::string& s) { g.msgPut(s); }
  void putS(const char* u8) { g.msgPut(S(u8)); }
  void num(long v) { g.msgNum(v); }

  // ---- drawing
  void statusWin(bool full);
  void statusUpdate() { statusWin(false); }
  void enemyNames();
  void drawEnemy(int a);
  void whiteEnemy(int a);
  void eraseEnemy(int a);
  void flashEnemy(int a);
  void vanish(int a);
  void shake();
  void castFlash();
  void cleanup();

  // ---- input
  void commandInput();
  int inputOne(int s);
  int enemySelect();
  int magicSelect(int s);
  int itemSelect();
  int memberSelect();
  int targetForMagic(int m, bool* cancel);

  // ---- AI / targeting
  int attackSide(int a) {
    int side;
    if (a < 4)
      side = 4;
    else
      side = (flags(a) & 0x04) ? 4 : 0;
    if (status(a) & 0x08) side = 4 - side;
    return side;
  }
  int mostHurt(int side);
  int lastDown(int side);
  int lastWithStatus(int side, int bits);
  int mostMp(int side);
  int randomWithout(int side, int bits);
  int randomTarget(int actor);
  bool canCast(int c, int m) const {
    const int cost = g.db.magics[m].mp;
    return g.db.heroes[c].magicLv[m] <= g.heroes[c].level && g.heroes[c].mp >= cost && g.heroes[c].mp / 4 >= cost;
  }
  bool maskHas(int a, int m) { return a >= 4 && enemyExists(a) && (E(a).magicMask[m >> 3] & (0x80 >> (m & 7))); }
  void decideParty(int s);
  void decideEnemy(int a);
  void computeOrder();

  // ---- execution
  int turn(int x);  // 0 = continue, 1 = battle ended
  enum HpRes { Down, Fell, Alive };
  HpRes hpChange(int t, int d);
  int damage(int a, int t);
  void showDamage(int t, int v);
  void defeated(int t);
  void doAction(int x);
  bool targetCheck(int x, int m, bool fromItem, int itemKind, int itemValue);
  int effect(int m, int caster, int t, int amount);
  void showResult(int m, int r, int actor);
  void groupProc(int s, int amount, int kind);
  void castMagic(int x, int m);
  void useItem(int x, int id);
  bool partyWiped();
  bool enemiesGone();
  void victory();
  void roundEnd();
};

// ================================================================ drawing
void Battle::statusWin(bool full) {
  if (full) g.window(42, 10, 79, 15);
  static const uint8_t hdr[7][12] = {
      {0xC6, 0xFC, 0x00, 0x00, 0xC6, 0x10, 0xC6, 0x00, 0x00, 0x00, 0xC6, 0xFC},
      {0xC6, 0xC6, 0x00, 0x00, 0xEE, 0x38, 0xC6, 0x00, 0x00, 0x00, 0xEE, 0xC6},
      {0xC6, 0xC6, 0x00, 0x00, 0xFE, 0x38, 0x6C, 0x00, 0x00, 0x00, 0xFE, 0xC6},
      {0xFE, 0xFC, 0x18, 0x00, 0xFE, 0x6C, 0x38, 0x00, 0x00, 0x00, 0xFE, 0xFC},
      {0xC6, 0xC0, 0x18, 0x00, 0xD6, 0x7C, 0x6C, 0x00, 0x00, 0x00, 0xD6, 0xC0},
      {0xC6, 0xC0, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0xC6, 0xC0},
      {0xC6, 0xC0, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0xC6, 0xC0}};
  for (int r = 0; r < 7; ++r)
    for (int b = 0; b < 12; ++b) g.scr_.bitmap1(&hdr[r][b], 1, 520 + b * 8, 248 + r, 15);
  for (int s = 0; s < 4; ++s) {
    if (!valid(s)) continue;
    const int c = g.party[s];
    g.drawStr(45, 11 + s, g.heroName(c));
    char b[64];
    std::snprintf(b, sizeof b, "%4d/%4d %4d", g.heroes[c].hp, g.heroes[c].recMaxHp, g.heroes[c].mp);
    g.drawStr(63, 11 + s, b);
    const std::string st = g.statusStr(c);
    if (!st.empty()) g.drawStr(73, 11 + s, st);
  }
}

void Battle::enemyNames() {
  g.window(20, 10, 41, 15);
  int row = 0;
  for (int i = 0; i < k; ++i)
    if (en[i].hp != 0) g.drawStr(23, 11 + row++, core::rawName(en[i].name, 16));
}

void Battle::drawEnemy(int a) {
  const BEnemy& e = E(a);
  g.scr_.blit(g.db.enemyImg[e.image % 48], e.col * 16, 128, true);
}

void Battle::whiteEnemy(int a) {
  const BEnemy& e = E(a);
  g.scr_.orSilhouette(g.db.enemyImg[e.image % 48], e.col * 16, 128, 80, 96, 15);
}

void Battle::eraseEnemy(int a) { g.scr_.fillRect(E(a).col * 16, 128, 81, 97, 0); }

void Battle::flashEnemy(int a) {
  g.se(22);
  for (int i = 0; i < 3; ++i) {
    g.waitMs(50);
    whiteEnemy(a);
    g.waitMs(50);
    drawEnemy(a);
  }
}

void Battle::vanish(int a) {
  for (int q = 0; q < 4; ++q) {
    const int t = q * q * 12 + 1;
    g.waitMs(t);
    drawEnemy(a);
    g.waitMs(t);
    eraseEnemy(a);
  }
}

void Battle::shake() {
  g.se(22);
  for (int i = 0; i < 3; ++i) {
    g.scr_.hOffset = 1;
    g.waitMs(40);
    g.scr_.hOffset = 0;
    g.waitMs(40);
  }
}

void Battle::castFlash() {
  for (int d = 1; d <= 12; ++d) {
    g.applyPalRaw(g.db.palet2, d % 12);
    g.waitMs(30);
  }
}

void Battle::cleanup() {
  for (int s = 0; s < 4; ++s)
    if (valid(s)) g.heroes[g.party[s]].status &= 3;
  g.scr_.clear(14);
  g.applyPal(g.palMode, 0);
  g.scr_.frameBorder = true;
  g.scr_.scrollLine = 0;
  g.scr_.hOffset = 0;
  g.redrawScreen();
}

// ================================================================ input (§6)
void Battle::commandInput() {
  bool mark[4] = {false, false, false, false};
  auto skip = [&](int s) {
    if (!valid(s)) return true;
    const Hero& h = g.heroes[g.party[s]];
    return h.hp == 0 || (h.status & 0x1A) != 0;
  };
  int s = 0;
  while (s < 4) {
    if (skip(s)) {
      ++s;
      continue;
    }
    mark[s] = true;
    const int r = inputOne(s);
    if (r == 2) return;
    if (r == 1) {
      int p = -1;
      for (int q = s - 1; q >= 0; --q)
        if (mark[q]) {
          p = q;
          break;
        }
      s = p >= 0 ? p : 0;
      continue;
    }
    ++s;
  }
}

int Battle::targetForMagic(int m, bool* cancel) {
  *cancel = false;
  switch (kTypeTab[m & 31]) {
    case 1: {
      const int t = memberSelect();
      if (t < 0) *cancel = true;
      return t;
    }
    case 2: {
      enemyNames();
      const int t = enemySelect();
      if (t < 0) *cancel = true;
      return t;
    }
    case 0:
      return 0;
    default:
      return 4;
  }
}

int Battle::inputOne(int s) {
  for (;;) {
    statusWin(true);
    g.scr_.xorRect(352, (11 + s) * 24 - 1, 273, 18, 15);
    enemyNames();
    g.window(0, 10, 19, 15);
    MenuRes r = g.menu({S("戦う　　"), S("魔法　　"), S("アイテム"), S("逃げる　"), S("自動戦闘")}, 8, 1, 5, 2, 11, 0,
                       false);
    if (r.kind == MenuRes::Cancel) return 1;
    switch (r.pos) {
      case 0: {
        inCmd[s] = -1;
        const int t = enemySelect();
        if (t < 0) continue;
        inTarget[s] = t;
        return 0;
      }
      case 1: {
        const int m = magicSelect(s);
        if (m < 0) continue;
        inCmd[s] = m;
        bool cancel;
        const int t = targetForMagic(m, &cancel);
        if (cancel) continue;
        inTarget[s] = t;
        return 0;
      }
      case 2: {
        const int id = itemSelect();
        if (id < 0) continue;
        inCmd[s] = id + 32;
        const ItemDef& it = g.db.items[id];
        if (it.kind <= 6) return 0;
        if (it.kind <= 10) {
          const int t = memberSelect();
          if (t < 0) continue;
          inTarget[s] = t;
          return 0;
        }
        bool cancel;
        const int t = targetForMagic(it.value & 31, &cancel);
        if (cancel) continue;
        inTarget[s] = t;
        return 0;
      }
      case 3:
        for (int q = 0; q < 4; ++q) inCmd[q] = -2;
        return 2;
      default:
        for (int q = 0; q < 4; ++q) inCmd[q] = -3;
        return 2;
    }
  }
}

int Battle::enemySelect() {
  enemyNames();
  std::vector<std::string> names;
  std::vector<int> ids;
  for (int i = 0; i < k; ++i)
    if (en[i].hp != 0) {
      names.push_back(core::rawName(en[i].name, 16));
      ids.push_back(4 + i);
    }
  if (ids.empty()) return -1;
  MenuRes r = g.menu(names, 16, 1, int(ids.size()), 21, 11, 0, false);
  if (r.kind != MenuRes::Selected) return -1;
  return ids[r.pos];
}

int Battle::magicSelect(int s) {
  const int c = g.party[s];
  std::vector<int> list;
  for (int m = 0; m < 32; ++m)
    if (g.db.heroes[c].magicLv[m] <= g.heroes[c].level) list.push_back(m);
  if (list.empty()) {
    g.window(42, 10, 79, 15);
    g.drawStr(52, 12, S("魔法は使えません"));
    g.waitRelease();
    g.waitPress();
    return -1;
  }
  g.window(24, 10, 79, 15);
  int top = 0, pos = 0;
  for (;;) {
    g.drawStr(27, 10, S("← Page ") + std::to_string(top / 4 + 1) + S(" →"));
    std::vector<std::string> items;
    for (int j = 0; j < 8; ++j) {
      const int idx = (top + j) % 32;
      if (idx < int(list.size()))
        items.push_back(core::rawName(g.db.magics[list[idx]].name, 16) + fmt(" %2ldp.", g.db.magics[list[idx]].mp));
      else
        items.push_back(S("　　　　　　　　　　　"));
    }
    MenuRes r = g.menu(items, 22, 2, 4, 25, 11, pos, true);
    if (r.kind == MenuRes::Cancel) return -1;
    if (r.kind == MenuRes::Next) {
      top = (top + 4) % 32;
      pos = r.pos;
      continue;
    }
    if (r.kind == MenuRes::Prev) {
      top = (top + 28) % 32;
      pos = r.pos;
      continue;
    }
    pos = r.pos;
    const int idx = (top + r.pos) % 32;
    if (idx >= int(list.size())) continue;
    return list[idx];
  }
}

int Battle::itemSelect() {
  g.window(24, 10, 79, 15);
  int top = (g.invCursor_ / 10) * 10, pos = g.invCursor_ % 10;
  for (;;) {
    g.drawStr(27, 10, S("← Page ") + fmt("%-2ld", top / 5 + 1) + S("→"));
    std::vector<std::string> items;
    for (int j = 0; j < 10; ++j) {
      const int sl = (top + j) % 50;
      if (g.invId[sl] >= 0x80)
        items.push_back(S("　　　　　　　　　　　"));
      else
        items.push_back(core::rawName(g.db.items[g.invId[sl]].name, 16) + S(" ×") + fmt("%2ld", g.invCnt[sl]));
    }
    MenuRes r = g.menu(items, 22, 2, 5, 25, 11, pos, true);
    if (r.kind == MenuRes::Cancel) return -1;
    if (r.kind == MenuRes::Next) {
      top = (top + 5) % 50;
      pos = r.pos;
      continue;
    }
    if (r.kind == MenuRes::Prev) {
      top = (top + 45) % 50;
      pos = r.pos;
      continue;
    }
    const int sl = (top + r.pos) % 50;
    if (g.invId[sl] >= 0x80) {
      pos = r.pos;
      continue;
    }
    g.invCursor_ = sl;
    return g.invId[sl];
  }
}

int Battle::memberSelect() {
  g.window(42, 10, 79, 15);
  for (int s = 0; s < 4; ++s) {
    if (!valid(s)) continue;
    const int c = g.party[s];
    g.drawStr(45, 11 + s, g.heroName(c));
    char b[64];
    std::snprintf(b, sizeof b, "%4d/%4d %4d", g.heroes[c].hp, g.heroes[c].recMaxHp, g.heroes[c].mp);
    g.drawStr(63, 11 + s, b);
    const std::string st = g.statusStr(c);
    if (!st.empty()) g.drawStr(73, 11 + s, st);
  }
  return g.heroSelect(0, 43, 11, false);
}

// ================================================================ targeting (§11)
int Battle::mostHurt(int side) {
  int best = -1, bv = 1;
  if (side < 4) {
    for (int s = 0; s < 4; ++s) {
      if (!alive(s)) continue;
      const int ratio = recMax(s) / hp(s);
      if (ratio > bv) {
        bv = ratio;
        best = s;
      }
    }
    return best;
  }
  for (int a = 4; a < 9; ++a) {
    if (!alive(a) || hidden[a]) continue;
    const int ratio = E(a).maxHp / E(a).hp;
    if (ratio > bv) {
      bv = ratio;
      best = a;
    }
  }
  if (best >= 0) return best;
  bv = 0;
  for (int a = 4; a < 9; ++a) {
    if (!alive(a) || hidden[a]) continue;
    const int diff = E(a).maxHp - E(a).hp;
    if (diff > bv) {
      bv = diff;
      best = a;
    }
  }
  return best;
}

int Battle::lastDown(int side) {
  int r = -1;
  if (side < 4) {
    for (int s = 0; s < 4; ++s)
      if (g.party[s] != 0xFF && valid(s) && hp(s) == 0) r = s;
  } else {
    for (int a = 4; a < 9; ++a)
      if (enemyExists(a) && E(a).maxHp != 0 && E(a).hp == 0) r = a;
  }
  return r;
}

int Battle::lastWithStatus(int side, int bits) {
  int r = -1;
  const int b = side < 4 ? 0 : 4, e = side < 4 ? 4 : 9;
  for (int a = b; a < e; ++a)
    if (alive(a) && (status(a) & bits)) r = a;
  return r;
}

int Battle::mostMp(int side) {
  if (side >= 4) return -1;
  int best = -1, bv = -1;
  for (int s = 0; s < 4; ++s) {
    if (!alive(s)) continue;
    const int mp = g.heroes[g.party[s]].mp;
    if (mp > bv) {
      bv = mp;
      best = s;
    }
  }
  return best;
}

int Battle::randomWithout(int side, int bits) {
  if (side < 4) {
    const int st = g.rng.next() % 4;
    for (int q = 0; q < 4; ++q) {
      const int s = (st + q) % 4;
      if (alive(s) && !(status(s) & bits)) return s;
    }
    return -1;
  }
  const int st = g.rng.next() % 5;
  for (int q = 0; q < 5; ++q) {
    const int a = 4 + (st + q) % 5;
    if (alive(a) && !(status(a) & bits)) return a;
  }
  return -1;
}

int Battle::randomTarget(int actor) {
  const int side = attackSide(actor);
  if (side == 4) {
    const int t = mostHurt(4);
    if (t >= 0) return t;
    int cand = g.rng.next() % 5 + 4;
    for (int tries = 0; tries < 64; ++tries) {
      if (alive(cand) && (!hidden[cand] || tries >= 5)) return cand;
      cand = cand == 8 ? 4 : cand + 1;
    }
    return cand;
  }
  const int r = g.rng.next() % 16;
  int s = r <= 9 ? 0 : (r <= 14 ? 1 : 2);
  if ((s == 1 || s == 2) && !alive(s)) s = 0;
  for (int q = 0; q < 8; ++q) {
    if (alive(s)) return s;
    s = (s + 1) % 4;
  }
  return s;
}

void Battle::decideParty(int s) {
  const int c = g.party[s];
  if (inCmd[s] != -3 && !(status(s) & 0x08)) {
    curAct = inCmd[s];
    curTarget = inTarget[s];
    if (curAct == -1 && !alive(curTarget)) curTarget = randomTarget(s);
    return;
  }
  const int own = 4 - attackSide(s);
  if (status(s) & 0x04) {
    curTarget = randomTarget(s);
    curAct = -1;
    return;
  }
  curTarget = lastWithStatus(own, 0x02);
  if (curTarget >= 0 && canCast(c, 7)) {
    curAct = 7;
    return;
  }
  curTarget = lastWithStatus(own, 0x08);
  if (curTarget >= 0 && canCast(c, 11)) {
    curAct = 11;
    return;
  }
  curTarget = mostHurt(own);
  int best = -1;
  for (int m = 0; m <= 4; ++m)
    if (canCast(c, m)) best = m;
  if (curTarget >= 0 && best >= 0) {
    curAct = best;
    return;
  }
  curTarget = randomTarget(s);
  if (curTarget < 4) {
    curAct = -1;
    return;
  }
  const int est = bAtk[s] - E(curTarget).def / 2;
  best = -1;
  for (int m = 20; m <= 30; ++m)
    if (canCast(c, m) && maskHas(curTarget, m) && !hidden[curTarget] && g.db.magics[m].power >= est) best = m;
  curAct = best >= 0 ? best : -1;
}

void Battle::decideEnemy(int a) {
  BEnemy& e = E(a);
  const bool anyBits = (e.act[0] | e.act[1] | e.act[2] | e.act[3] | e.act[4]) != 0;
  auto bit = [&](int n) { return (e.act[n >> 3] & (0x80 >> (n & 7))) != 0; };
  int pos = 0;
  int tries = 0;
  const int atk = attackSide(a), own = 4 - atk;
  for (;;) {
    ++tries;
    if (anyBits) {
      const int m = g.rng.next() % 40;
      for (int rep = 0; rep <= m; ++rep) {
        do {
          pos = (pos + 1) % 40;
        } while (!bit(pos));
        if (pos == 0 && g.rng.next() < 3000) break;
      }
    }
    bool ok = true;
    if (pos == 0) {
      curAct = -1;
      curTarget = randomTarget(a);
    } else if (pos <= 7) {
      static const int acts[8] = {-1, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
      curAct = acts[pos];
      if (pos == 4) curTarget = randomTarget(a);
      if (pos == 5) curTarget = atk;
      if (pos == 6) curTarget = mostMp(atk);
    } else {
      const int m = pos - 8;
      curAct = m;
      if (pos <= 10) {
        curTarget = mostHurt(own);
        ok = curTarget >= 0;
      } else if (pos <= 12) {
        curTarget = own;
      } else if (pos == 13) {
      } else if (pos == 14) {
        curTarget = lastDown(own);
        ok = curTarget >= 0;
      } else if (pos == 15) {
        curTarget = lastWithStatus(own, 0x03);
        ok = curTarget >= 0;
      } else if (pos == 16) {
        curTarget = randomWithout(atk, 0x1B);
        ok = curTarget >= 0;
      } else if (pos == 17 || pos == 19 || pos == 20) {
        curTarget = randomWithout(atk, 0x1A);
        ok = curTarget >= 0;
      } else if (pos == 18) {
        curTarget = randomWithout(atk, 0x1E);
        ok = curTarget >= 0;
      } else if (pos == 21) {
        curTarget = randomTarget(a);
        ok = curTarget >= 0;
      } else if (pos <= 24) {
        curTarget = randomWithout(own, 0x05);
        ok = curTarget >= 0;
      } else if (pos <= 30) {
        curTarget = randomTarget(a);
      } else if (pos <= 33) {
        curTarget = atk;
      } else if (pos <= 36) {
        curTarget = randomTarget(a);
      } else {
        curTarget = atk;
      }
    }
    if (!ok) {
      if (tries > 10) {
        curAct = 0x39;
        break;
      }
      continue;
    }
    break;
  }
  if ((e.flags & 0x10) && pos >= 22 && pos <= 24) curTarget = a;
  if (e.flags & 0x08) curTarget = a;
}

void Battle::computeOrder() {
  int speed[9];
  for (int a = 0; a < 9; ++a) {
    order[a] = a;
    if (!alive(a) || (status(a) & 0x12)) {
      speed[a] = -1;
      canAct[a] = false;
    } else {
      speed[a] = a < 4 ? bAgi[a] : E(a).agi;
      canAct[a] = true;
    }
  }
  for (int i = 0; i < 9; ++i) {
    int idx = -1, bv = -1;
    for (int j = i; j < 9; ++j)
      if (speed[j] >= 0 && speed[j] >= bv) {
        bv = speed[j];
        idx = j;
      }
    if (idx >= 0) {
      std::swap(speed[i], speed[idx]);
      std::swap(order[i], order[idx]);
    }
  }
  for (int p = 0; p < 8; p += 2)
    if (g.rng.next() < 16000) {
      std::swap(order[p], order[p + 1]);
      std::swap(speed[p], speed[p + 1]);
    }
}

// ================================================================ execution (§12-14)
Battle::HpRes Battle::hpChange(int t, int d) {
  int& h = hp(t);
  if (d < 0 && h == 0) return Down;
  if (h - d <= 0) {
    h = 0;
    return Fell;
  }
  const int cap = t < 4 ? (valid(t) ? g.calcMaxHp(g.party[t]) : 0) : E(t).maxHp;
  h = std::min(h - d, cap);
  return Alive;
}

int Battle::damage(int a, int t) {
  const int atk = a < 4 ? bAtk[a] : E(a).atk;
  const int def = t < 4 ? bDef[t] : E(t).def;
  int d = atk - def / 2;
  if (d < 0)
    d = g.rng.next() > 15000 ? 0 : 1;
  else
    d = g.variance(d);
  if (t < 4 && g.rng.next() < bLuck[t] * 10) d = 0;
  if (t >= 4 && guard[t]) d /= 2;
  const int thr = a < 4 ? 5000 : ((flags(a) & 0x02) ? 20000 : 2000);
  if (g.rng.next() < thr) {
    putS(a < 4 ? "会心" : "痛恨");
    putS("の一撃！#");
    d += d / 2;
  }
  if (a >= 4 && (flags(a) & 0x01) && g.rng.next() < 16000) d = 0;
  return hpChange(t, d) == Alive ? d : -d;
}

void Battle::defeated(int t) {
  put(name(t));
  if (t < 4) {
    putS("は気絶してしまった‥‥");
  } else {
    putS("を倒した！");
    vanish(t);
  }
}

void Battle::showDamage(int t, int v) {
  put(name(t));
  putS("は");
  if (v == 0) {
    g.se(29);
    putS("攻撃をかわした！#;;");
    return;
  }
  num(v < 0 ? -v : v);
  putS(" p.のダメージ！#");
  if (t >= 4)
    flashEnemy(t);
  else
    shake();
  if (v < 0) defeated(t);
  if (t < 4) statusUpdate();
}

void Battle::doAction(int x) {
  g.se(x < 4 ? 10 : 12);
  switch (curAct) {
    case -1:
      putS("の攻撃！#;");
      if (curTarget >= 4 && hidden[curTarget]) {
        put(name(curTarget));
        putS("は隠れている‥‥;");
        return;
      }
      showDamage(curTarget, damage(x, curTarget));
      return;
    case 0x33:
      putS("は身構えた！;");
      guard[x] = true;
      return;
    case 0x34:
      putS("は身を隠した！;");
      hidden[x] = true;
      vanish(x);
      return;
    case 0x35:
      putS("は逃げ出した！;");
      E(x).maxHp = 0;
      E(x).hp = 0;
      vanish(x);
      return;
    case 0x36: {
      putS("が突撃してきた！#;");
      const int t = curTarget;
      showDamage(t, damage(x, t));
      if (!alive(t)) return;
      put(name(t));
      putS("の反撃！#;");
      guard[x] = true;
      curTarget = x;
      showDamage(x, damage(t, x));
      for (int p = 0; p < 9; ++p)
        if (order[p] == t) order[p] = 0xFF;
      return;
    }
    case 0x37:
      putS("が自爆した！#;");
      E(x).hp = 0;
      vanish(x);
      groupProc(curTarget, E(x).maxHp / 4, kFixed);
      return;
    case 0x38: {
      putS("の攻撃！#;");
      const int amt = g.variance(10);
      // PROVISIONAL(Q2): a "none" target (attack side = enemies) is undefined in the spec; nothing is drained.
      if (curTarget >= 0 && curTarget < 4 && valid(curTarget)) {
        int& mp = g.heroes[g.party[curTarget]].mp;
        if (mp >= amt) mp -= amt;
      }
      put(name(curTarget));
      putS("はＭＰを");
      num(amt);
      putS(" p.奪われた！;");
      statusUpdate();
      return;
    }
    default:
      putS("は　じっとしている‥‥;");
      return;
  }
}

bool Battle::targetCheck(int x, int m, bool fromItem, int itemKind, int itemValue) {
  if (x >= 4) return true;
  if (fromItem) {
    if (itemKind < 7 || itemKind > 11) return true;
    m = itemKind == 11 ? itemValue : 0;
  }
  if (m == 6) return true;
  const int type = kTypeTab[m & 31];
  if (type == 0 || type == 4) return true;
  const int t = curTarget;
  if (alive(t)) return true;
  if (t >= 4 && t <= 8 && hidden[t]) {
    put(name(t));
    putS("は隠れている‥‥;");
    return false;
  }
  put(name(t));
  putS("は");
  if (t >= 4)
    putS(enemyExists(t) && E(t).maxHp == 0 ? "いない‥‥#" : "死んでいる‥‥#");
  else
    putS("気絶している‥‥");
  return false;
}

int Battle::effect(int m, int caster, int t, int amount) {
  const bool fromItem = caster == kItemCaster;
  bool direct;
  if (!fromItem) {
    if (caster >= 4 && (flags(caster) & 0x01) && g.rng.next() < 16000) return 0;
    if (caster >= 0 && caster < 4) {
      Hero& h = g.heroes[g.party[caster]];
      const int cost = g.db.magics[m].mp;
      if (h.mp - cost < 0) {
        putS("ＭＰが足りない！#;");
        return kAbort;
      }
      h.mp -= cost;
    }
    if (caster >= 0 && (status(caster) & 0x04)) {
      putS("しかし、魔法は封じられている！#;");
      return kAbort;
    }
    direct = t < 4 || m == 5 || (caster >= 0 && isAllMagic(m));
  } else {
    direct = t < 4 || isAllMagic(m);
  }
  if (!direct) {
    if (!maskHas(t, m)) {
      put(name(t));
      putS("には効かなかった！#;");
      return kAbort;
    }
    if (hidden[t]) {
      put(name(t));
      putS("は隠れている‥‥;");
      return kAbort;
    }
  }
  int power = g.db.magics[m].power;
  if (caster >= 4 && (flags(caster) & 0x20)) power = g.variance(std::min(power * 2, 9999));
  const bool hasCaster = caster != kNoCaster;
  auto clampStat = [](int v) { return std::max(0, std::min(9999, v)); };
  auto stat = [&](int which) -> int& {
    static int dummy;
    if (t < 4) {
      if (t < 0) return dummy;
      return which == 0 ? bAtk[t] : which == 1 ? bDef[t] : bAgi[t];
    }
    if (!enemyExists(t)) return dummy;
    return which == 0 ? E(t).atk : which == 1 ? E(t).def : E(t).agi;
  };
  switch (m) {
    case 0:
    case 1:
    case 2:
      hpChange(t, -power);
      return power;
    case 3:
    case 4:
      if (hasCaster) return power;
      hpChange(t, -amount);
      return amount;
    case 5:
      return (caster == kItemCaster || (caster >= 0 && caster < 4)) ? 0 : 1;
    case 6: {
      if (hp(t) != 0) return 0;
      hp(t) = std::max(1, recMax(t) / 8);
      return hp(t);
    }
    case 7: {
      const int r = status(t) & 3;
      status(t) &= ~3;
      return r;
    }
    case 8: status(t) |= 0x01; return 1;
    case 9: status(t) |= 0x02; return 1;
    case 10: status(t) |= 0x04; return 1;
    case 11:
      status(t) ^= 0x08;
      return (status(t) & 0x08) ? 9 : 1;
    case 12: status(t) |= 0x10; return 1;
    case 13: {
      const int v = g.rng.next();
      if (v < (t < 4 ? 25000 : 16000)) return 0;
      hp(t) = 0;
      return 1;
    }
    case 14: case 15: case 16: {
      int& s = stat(m - 14);
      s = clampStat(s + power);
      return -power;
    }
    case 17: case 18: case 19: {
      int& s = stat(m - 17);
      s = clampStat(s - power);
      return power;
    }
    case 20: case 21: case 22: case 26: case 27: case 28:
      return hpChange(t, power) == Fell ? -power : power;
    default:  // 23-25, 29-31
      if (hasCaster) return power;
      return hpChange(t, amount) == Fell ? -amount : amount;
  }
}

void Battle::groupProc(int s, int amount, int kind) {
  const int b = s < 4 ? 0 : 4, e = s < 4 ? 4 : 9;
  bool first = true;
  for (int a = b; a < e; ++a) {
    curTarget = a;
    if (!alive(a)) continue;
    if (!first && kind != 3 && kind != 4) put("@");
    first = false;
    if (kind == kFixed) {
      if (a >= 4 && hidden[a]) {
        put(name(a));
        putS("は隠れている‥‥;");
        break;
      }
      showDamage(a, hpChange(a, amount) == Fell ? -amount : amount);
    } else if (kind == 3 || kind == 4) {
      effect(kind, kNoCaster, a, amount);
    } else {
      const int r = effect(kind, kNoCaster, a, amount);
      if (r != kAbort) showDamage(a, r);
    }
  }
  curTarget = s;
}

void Battle::showResult(int m, int r, int actor) {
  const int t = curTarget;
  switch (m) {
    case 0: case 1: case 2:
      put(name(t));
      putS("のＨＰが");
      num(r);
      putS(" p.回復した！;");
      break;
    case 3: case 4:
      putS("ＨＰがそれぞれ");
      num(r);
      putS(" p.ずつ回復した！#;");
      groupProc(curTarget, r, m);
      break;
    case 5:
      putS("消えてしまった‥‥;");
      if (actor >= 4) {
        E(actor).maxHp = 0;
        E(actor).hp = 0;
        vanish(actor);
      } else {
        statusUpdate();
      }
      break;
    case 6:
      put(name(t));
      putS("が生き返った！;");
      if (t >= 4) drawEnemy(t);
      break;
    case 7:
      put(name(t));
      if (r & 1) putS("の毒が消えた！#;");
      if (r & 2) {
        if (r & 1) put(name(t));
        putS("の麻痺が回復した！#;");
      }
      break;
    case 8: put(name(t)); putS("は毒に侵された！;"); break;
    case 9: put(name(t)); putS("は麻痺してしまった！;"); break;
    case 10: put(name(t)); putS("は魔法を封じられた！;"); break;
    case 11: put(name(t)); putS(r == 1 ? "は正気に戻った！;" : "は混乱してしまった！;"); break;
    case 12: put(name(t)); putS("は眠ってしまった！;"); break;
    case 13: defeated(t); break;
    case 14: case 15: case 16: case 17: case 18: case 19: {
      static const char* what[3] = {"の攻撃力", "の防御力", "のすばやさ"};
      put(name(t));
      putS(what[(m - 14) % 3]);
      putS("が#");
      num(r < 0 ? -r : r);
      putS(m < 17 ? " p.上がった！;" : " p.下がった！;");
      break;
    }
    case 20: case 21: case 22: case 26: case 27: case 28:
      showDamage(t, r);
      break;
    default:
      groupProc(curTarget, r, m);
      break;
  }
}

void Battle::castMagic(int x, int m) {
  putS("は#");
  put(core::rawName(g.db.magics[m].name, 17));
  putS("の魔法を唱えた！#");
  g.se(m <= 7 ? 13 : 20);
  castFlash();
  const bool ok = targetCheck(x, m, false, 0, 0);
  const int r = effect(m, x, curTarget, 0);
  if (x < 4) statusUpdate();
  if (!ok || r == kAbort) return;
  if (r == 0) {
    putS("しかし、失敗してしまった！;");
    return;
  }
  showResult(m, r, x);
  if (curTarget >= 0 && curTarget < 4) statusUpdate();
}

void Battle::useItem(int x, int id) {
  const ItemDef& it = g.db.items[id];
  putS("は#");
  put(core::rawName(it.name, 17));
  if (!g.hasItem(id)) {
    putS("を持っていない！;");
    return;
  }
  putS("を使った！#");
  if (!targetCheck(x, 0, true, it.kind, it.value)) return;
  if (it.kind >= 7 && it.kind <= 11) g.removeItem(id, false);
  const int amt = g.variance(it.value);
  const int t = curTarget;
  bool ok = false;
  switch (it.kind) {
    case 7:
      hpChange(t, -amt);
      if (alive(t)) {
        put(name(t));
        putS("のＨＰが");
        num(amt);
        putS(" p.回復した！;");
        ok = true;
      }
      break;
    case 8:
      if (alive(t) && t < 4) {
        const int c = g.party[t];
        g.heroes[c].mp = std::min(g.heroes[c].mp + amt, g.calcMaxMp(c));
        put(name(t));
        putS("のＭＰが");
        num(amt);
        putS(" p.回復した！;");
        ok = true;
      }
      break;
    case 9:
      if (alive(t) && (status(t) & 1)) {
        put(name(t));
        putS("の毒が消えた！;");
        status(t) &= ~1;
        ok = true;
      }
      break;
    case 10:
      if (alive(t) && (status(t) & 2)) {
        put(name(t));
        putS("の麻痺が回復した！;");
        status(t) &= ~2;
        ok = true;
      }
      break;
    case 11: {
      const int m = it.value & 31;
      curAct = m;
      const bool v = targetCheck(x, m, false, 0, 0);
      const int r = effect(m, kItemCaster, curTarget, 0);
      if (v && r != kAbort) {
        if (r == 0)
          putS("しかし、失敗してしまった！;");
        else {
          showResult(m, r, x);
          if (curTarget >= 0 && curTarget < 4) statusUpdate();
        }
      }
      ok = true;
      break;
    }
    default:
      break;
  }
  if (!ok) putS("しかし、何も起きなかった！;");
  statusUpdate();
}

bool Battle::partyWiped() {
  for (int s = 0; s < 4; ++s)
    if (alive(s) && !(status(s) & 0x02)) return false;
  return true;
}

bool Battle::enemiesGone() {
  for (int a = 4; a < 9; ++a)
    if (alive(a)) return false;
  return true;
}

void Battle::victory() {
  putS("敵を全滅させた！#");
  g.bgmChange(g.fieldBgm);
  long ex = 0, gd = 0;
  for (int i = 0; i < k; ++i)
    if (en[i].maxHp != 0) {
      ex += en[i].exp;
      gd += en[i].gold;
    }
  putS("経験値をそれぞれ ");
  num(ex);
  putS("p.獲得！#");
  putS("お金を ");
  num(gd);
  putS(" 手に入れた！_");
  for (int s = 0; s < 4; ++s) {
    const int lv = g.addExp(s, int(ex));
    if (lv) {
      put(name(s));
      putS("はレベル");
      num(lv);
      putS("になった！_");
    }
  }
  g.gold = std::max(0L, std::min(99999999L, g.gold + gd));
}

int Battle::turn(int x) {
  const int times = (x >= 4 && (flags(x) & 0x80)) ? 2 : 1;
  for (int t = 0; t < times; ++t) {
    if (!alive(x)) return 0;
    if (status(x) & 0x10) {
      if (g.rng.next() < 8000) {
        status(x) &= ~0x10;
        if (x < 4) statusUpdate();
        put(name(x));
        putS("は目を覚ました！;@");
        return 0;
      }
    }
    if (status(x) & 0x08) {
      if (g.rng.next() < 8000) {
        status(x) &= ~0x08;
        if (x < 4) statusUpdate();
        put(name(x));
        putS("は正気に戻った！;@");
        return 0;
      }
    }
    if ((status(x) & 0x12) || !canAct[x]) return 0;
    if (x >= 4 && hidden[x]) return 0;
    if (x < 4)
      decideParty(x);
    else
      decideEnemy(x);
    put(name(x));
    if (x < 4 && curAct == -2) {
      putS("は逃げ出した！_");
      result = -1;
      g.bgmChange(g.fieldBgm);
      cleanup();
      return 1;
    }
    if (x < 4 && curAct >= 32)
      useItem(x, curAct - 32);
    else if (curAct >= 0 && curAct <= 31)
      castMagic(x, curAct);
    else
      doAction(x);
    if (x >= 4 && curAct != 0x33 && curAct != 0x39) guard[x] = false;
    put("@");
    if (partyWiped()) {
      putS("全滅してしまった‥‥_");
      result = 0;
      cleanup();
      return 1;
    }
    if (enemiesGone()) {
      victory();
      result = 1;
      cleanup();
      return 1;
    }
  }
  return 0;
}

void Battle::roundEnd() {
  for (int a = 4; a < 9; ++a) {
    if (!enemyExists(a) || !hidden[a]) continue;
    if (!alive(a)) {
      bool other = false;
      for (int b = 4; b < 9; ++b)
        if (b != a && alive(b) && !hidden[b]) other = true;
      if (other && g.rng.next() < 15000) continue;
    }
    drawEnemy(a);
    g.msgClear();
    put(name(a));
    putS("を見つけた！;@");
    hidden[a] = false;
  }
  for (int a = 4; a < 9; ++a) {
    if (!enemyExists(a)) continue;
    BEnemy& e = E(a);
    if (e.maxHp != 0 && e.hp == 0 && (e.flags & 0x40)) {
      if (g.rng.next() <= 5000) {
        e.hp = e.maxHp < 8 ? 1 : e.maxHp / 8;
        drawEnemy(a);
        put(name(a));
        putS("が生き返った！;@");
      }
    }
  }
  for (int a = 0; a < 9; ++a) {
    if (!alive(a) || !(status(a) & 1)) continue;
    const int dmg = a < 4 ? std::max(1, recMax(a) / 20) : std::max(1, E(a).maxHp / 10);
    hp(a) -= dmg;
    if (hp(a) <= 0) hp(a) = 1;
  }
  statusUpdate();
}

int Battle::run(int group, bool eventBattle) {
  g.scr_.frameBorder = false;
  g.scr_.clear(14);
  g.rng.seed(g.frameCounter_ & 0xFFFF);
  g.applyPalRaw(g.db.palet2, 0);
  group %= 32;
  int n = 0;
  for (int i = 0; i < 5; ++i)
    if (g.db.enemies[group * 5 + i].hp > 0) ++n;
  if (n == 0) {
    g.bgmChange(g.fieldBgm);
    cleanup();
    return 1;
  }
  if (n < 2) eventBattle = true;
  const int thin = eventBattle ? -1 : g.rng.next() % 8;
  k = 0;
  for (int i = 0; i < 5; ++i) {
    const EnemyDef& d = g.db.enemies[group * 5 + i];
    if (d.hp == 0 || i == thin) continue;
    BEnemy& e = en[k++];
    std::memcpy(e.name, d.name, 17);
    e.status = 0;
    e.image = d.image;
    e.hp = e.maxHp = d.hp;
    e.exp = d.exp;
    e.gold = d.gold;
    e.atk = d.atk;
    e.def = d.def;
    e.agi = d.agi;
    std::memcpy(e.magicMask, d.magicMask, 4);
    std::memcpy(e.act, d.act, 5);
    e.flags = d.flags;
  }
  for (int i = 0; i < k; ++i) en[i].col = i * 8 + (5 - k) * 4 + 1;
  for (int s = 0; s < 4; ++s) {
    if (!valid(s)) continue;
    const int c = g.party[s];
    Hero& h = g.heroes[c];
    h.status &= 3;
    h.recMaxHp = g.calcMaxHp(c);
    h.recMaxMp = g.calcMaxMp(c);
    bAtk[s] = g.calcAtk(c);
    bDef[s] = g.calcDef(c);
    bAgi[s] = g.calcAgi(c);
    bLuck[s] = g.calcLuck(c);
  }
  g.mH_ = 4;
  g.scr_.scrollLine = 250;
  g.window(16, 0, 63, 4);
  g.msgClear();
  putS("敵と遭遇した！");
  for (int s = 250; s != 10; s = (s + 10) % 400) {
    g.frame();
    g.scr_.scrollLine = s >= 384 ? 0 : s;
  }
  g.scr_.scrollLine = 0;
  g.scr_.fillRect(0, 90, 640, 148, 0);
  g.window(16, 0, 63, 4);
  g.msgClear();
  for (int i = 0; i < k; ++i)
    if (en[i].maxHp != 0) drawEnemy(4 + i);
  for (;;) {
    commandInput();
    statusWin(true);
    computeOrder();
    for (int p = 0; p < 9; ++p) {
      const int x = order[p];
      if (x == 0xFF) continue;
      if (turn(x)) return result;
    }
    roundEnd();
  }
}

int Game::battle(int group, bool eventBattle) {
  Battle b(*this);
  const int r = b.run(group, eventBattle);
  if (opt_.verbose) host_.log("[d1 battle " + std::to_string(group) + (eventBattle ? " event" : " random") +
                              " result " + std::to_string(r) + "]");
  return r;
}

}  // namespace d1
