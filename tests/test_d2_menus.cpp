// D2 menus / shops / inns / field item & magic use (spec 23) acceptance checks.
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/text.h"
#include "core/util.h"
#include "d2/game.h"
#include "d2/menus.h"
#include "test.h"

namespace {
const std::string kSamples = (t::samples() + "games/");
bool have(const std::string& p) { return core::isDirectory(p); }

struct ScriptEnd {};

// Scripted host: same key syntax as the headless runner ("z", "x", "u", "d", "l", "r", "N" waits,
// "k*N" holds). Throws ScriptEnd after the script plus `tail` idle frames.
struct ScriptHost : core::Host {
  std::vector<core::InputState> frames;
  size_t pos = 0;
  int tail = 2000;
  long count = 0;
  void script(const std::string& s) {
    frames.clear();
    pos = 0;
    std::istringstream ws(s);
    std::string w;
    while (ws >> w) {
      if (w[0] >= '0' && w[0] <= '9') {
        frames.insert(frames.end(), size_t(std::atoi(w.c_str())), core::InputState());
        continue;
      }
      core::InputState in;
      int n = 4;
      auto star = w.find('*');
      if (star != std::string::npos) n = std::atoi(w.c_str() + star + 1);
      for (char c : w.substr(0, star)) {
        if (c == 'z') in.ok = true;
        if (c == 'x') in.cancel = true;
        if (c == 'u') in.up = true;
        if (c == 'd') in.down = true;
        if (c == 'l') in.left = true;
        if (c == 'r') in.right = true;
        if (c == 'q') in.fast = true;
      }
      frames.insert(frames.end(), size_t(n), in);
      frames.insert(frames.end(), 4, core::InputState());
    }
  }
  core::InputState cur;
  std::function<void(long)> onFrame;  // called at the start of every VSYNC (tests inject keystrokes)
  void vsync(const core::Screen&) override {
    ++count;
    if (onFrame) onFrame(count);
    if (pos < frames.size()) {
      cur = frames[pos++];
    } else {
      cur = core::InputState();
      if (--tail < 0) throw ScriptEnd();
    }
  }
  core::InputState input() override { return cur; }
  void sound(const core::SoundRequest&) override {}
};

// A game positioned on its start map (as after "new game").
std::unique_ptr<d2::Game> startGame(ScriptHost& h, const std::string& root, bool testPlay = false) {
  core::StartOptions opt;
  opt.noTitle = testPlay;
  auto g = std::make_unique<d2::Game>(h, root, opt);
  g->scr.textEnabled = true;
  g->recalcAll();
  g->newGameInit();
  g->enterMap();
  g->fieldMask();
  g->drawField();
  g->setTone(100);
  return g;
}

[[maybe_unused]] std::string sj(const char* u) { return core::utf8ToSjis(u); }

// Symlink farm of a game root so tests can write SAVEn.DAT without touching the sample.
std::string farm(const std::string& root, const std::string& name) {
  const std::string tmp = (t::tmp() + "d2menu_") + name;
  std::string cmd = "rm -rf '" + tmp + "' && mkdir -p '" + tmp + "'";
  if (std::system(cmd.c_str()) != 0) return "";
  for (auto& n : core::listDir(root)) {
    std::string u = n;
    for (auto& c : u) c = char(std::toupper(uint8_t(c)));
    if (u.rfind("SAVE", 0) == 0) continue;
    if (::symlink((root + "/" + n).c_str(), (tmp + "/" + n).c_str()) != 0) return "";
  }
  return tmp;
}
}  // namespace

TEST(d2menu_variance_formula) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  core::StartOptions opt;
  d2::Game g(h, root, opt);
  // p <= 0: no random numbers drawn
  g.rng.seed(1);
  CHECK_EQ(d2::variance(g, 100, 0), 100);
  CHECK_EQ(g.rng.state, 1u);
  // p = 3, v = 100: v += r1%3 ; r = v*3/12 ; v += r/2 - r2%r
  g.rng.seed(1);
  core::Rng ref;
  ref.seed(1);
  int v = 100 + ref.next() % 3;
  const int r = v * 3 / 12;
  v += r / 2 - ref.next() % r;
  CHECK_EQ(d2::variance(g, 100, 3), v);
  CHECK_EQ(g.rng.state, ref.state);
  // r <= 1: only one random number
  g.rng.seed(5);
  ref.seed(5);
  const int v2 = 3 + ref.next() % 3;
  CHECK_EQ(d2::variance(g, 3, 1), v2);
  CHECK_EQ(g.rng.state, ref.state);
}

TEST(d2menu_magic_power) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  core::StartOptions opt;
  d2::Game g(h, root, opt);
  d2::Member c{};
  c.stat[0] = 300;
  c.stat[2] = 200;
  d2::Magic m{};
  m.power = 10;
  m.atkCoef = 200;  // 300*200 = 60000 -> int16 -5536 -> /10 = -553
  m.spiCoef = 0;
  m.variance = 0;
  CHECK_EQ(d2::magicPower(g, c, m), 1);  // 10 - 553 <= 1 -> 1
  m.atkCoef = 0;
  m.spiCoef = 15;  // 200*15/10 = 300
  CHECK_EQ(d2::magicPower(g, c, m), 310);
  m.power = 40000;
  CHECK_EQ(d2::magicPower(g, c, m), 30000);
}

TEST(d2menu_shop_buy_sell_inn) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root);
  std::memset(g->inventory, 0, sizeof(g->inventory));
  g->st.gold = 30;
  // buy: item 104 (price 12) twice, then 105 (price 20) -> not enough money, then cancel
  h.script("4 z 4 z 4 z 4 z 4 d 4 z 4 z 4 x");
  h.tail = 50;
  try {
    d2::shopBuy(*g, {104, 105});
  } catch (ScriptEnd&) {
    CHECK(!"shopBuy did not return");
  }
  CHECK_EQ(g->itemCount(104), 2);
  CHECK_EQ(g->itemCount(105), 0);
  CHECK_EQ(g->st.gold, 6u);
  CHECK_EQ(g->st.shopDone, 1);
  CHECK(g->embed == core::rawName(reinterpret_cast<const uint8_t*>(g->db.items[105].name), 16));
  // sell one 104 (price 12 -> 6): select, yes, key wait, cancel list
  g->st.shopDone = 0;
  h.script("4 z 4 z 4 z 4 x");
  h.tail = 50;
  try {
    d2::shopSell(*g);
  } catch (ScriptEnd&) {
    CHECK(!"shopSell did not return");
  }
  CHECK_EQ(g->itemCount(104), 1);
  CHECK_EQ(g->st.gold, 12u);
  CHECK_EQ(g->st.shopDone, 1);
  // selling a price-0 item is refused
  g->addItem(13);  // "片刃の剣" (single-edged sword), price 0
  g->moveItemToEnd(104);
  h.script("4 z 4 z 4 x");
  h.tail = 50;
  try {
    d2::shopSell(*g);
  } catch (ScriptEnd&) {
    CHECK(!"shopSell did not return");
  }
  CHECK_EQ(g->itemCount(13), 1);
  CHECK_EQ(g->st.gold, 12u);
  // inn: price 10, yes; party fully healed
  d2::Member& m = g->members[g->st.party[0]];
  m.hp = 1;
  m.status[0] = 1;
  h.script("4 z");
  h.tail = 400;
  try {
    d2::inn(*g, 10);
  } catch (ScriptEnd&) {
    CHECK(!"inn did not return");
  }
  CHECK_EQ(g->st.gold, 2u);
  CHECK_EQ(m.hp, m.maxHp);
  CHECK_EQ(m.status[0], 0);
  CHECK_EQ(g->st.poison, 0);
  CHECK_EQ(g->st.shopDone, 1);
  // inn: not enough money
  m.hp = 1;
  h.script("4 z 4 z");
  h.tail = 50;
  try {
    d2::inn(*g, 10);
  } catch (ScriptEnd&) {
    CHECK(!"inn did not return");
  }
  CHECK_EQ(g->st.gold, 2u);
  CHECK_EQ(m.hp, 1);
  CHECK_EQ(g->st.shopDone, 0);
  // inn: negative price -> gold becomes 0 (unsigned subtraction)
  g->st.gold = 50;
  h.script("4 z");
  h.tail = 400;
  try {
    d2::inn(*g, -5);
  } catch (ScriptEnd&) {
  }
  CHECK_EQ(g->st.gold, 0u);
}

TEST(d2menu_field_item_use) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root);
  std::memset(g->inventory, 0, sizeof(g->inventory));
  g->addItem(104);  // "傷薬" (salve): heal 25, consumable
  g->addItem(104);
  d2::Member& m = g->members[g->st.party[0]];
  m.hp = 1;
  // menu: item -> 104 -> use -> target 0 -> key wait -> cancel menu
  h.script("8 z 8 z 8 z 8 z 60 z 8 x");
  h.tail = 60;
  try {
    d2::fieldMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"menu did not close");
  }
  CHECK_EQ(m.hp, std::min(26, int(m.maxHp)));
  CHECK_EQ(g->itemCount(104), 1);
  // item with no effect (weapon "use"): nothing happens, not consumed
  g->addItem(1);
  g->moveItemToEnd(104);  // 1 first
  h.script("8 z 8 z 8 z 60 z 8 x");
  h.tail = 60;
  try {
    d2::fieldMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"menu did not close");
  }
  CHECK_EQ(g->itemCount(1), 1);
  // discard: item 1 -> "捨てる" (Discard) -> "捨てる" (confirm)
  h.script("8 z 8 z 8 d d z 8 u z 8 x");
  h.tail = 60;
  try {
    d2::fieldMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"menu did not close");
  }
  CHECK_EQ(g->itemCount(1), 0);
  CHECK_EQ(g->itemCount(104), 1);
  CHECK_EQ(g->inventory[0][0], 104);
}

TEST(d2menu_equip_change) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root);
  std::memset(g->inventory, 0, sizeof(g->inventory));
  const int c = g->st.party[0];
  d2::Member& m = g->members[c];
  for (auto& e : m.equip) e = 0xFF;
  g->recalc(m);
  const int atk0 = m.stat[0];
  g->addItem(35);  // armor first: not a weapon candidate
  g->addItem(2);   // "木刀" (wooden sword) +7 atk
  // menu: equip (cmd 2) -> member 0 -> slot 0 -> candidate 0 (wooden sword) -> cancel slot -> cancel menu
  h.script("8 d d z 8 z 8 z 8 z 8 x 8 x");
  h.tail = 60;
  try {
    d2::fieldMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"menu did not close");
  }
  CHECK_EQ(m.equip[0], 2);
  CHECK_EQ(m.stat[0], atk0 + 7);
  CHECK_EQ(g->itemCount(2), 0);
  // unequip by choosing an empty slot of the candidate list
  h.script("8 d d z 8 z 8 z 8 r z 8 x 8 x");
  h.tail = 60;
  try {
    d2::fieldMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"menu did not close");
  }
  CHECK_EQ(m.equip[0], 0xFF);
  CHECK_EQ(g->itemCount(2), 1);
  CHECK_EQ(m.stat[0], atk0);
}

TEST(d2menu_debug_menu_switch_toggle) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root, true);
  std::memset(g->switches, 0, sizeof(g->switches));
  g->st.noEncounter = 0;
  // system(4) -> debug(2) -> switches 200-399 (1) -> right right down -> toggle -> exit ->
  // encounter toggle (6) -> exit debug -> exit system -> close menu
  h.script("8 d d d d z 8 d d z 8 d z 8 r r d z 8 x 8 d d d d d z 8 x 8 x 8 x");
  h.tail = 60;
  try {
    d2::fieldMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"menu did not close");
  }
  CHECK_EQ(g->switches[212], 1);
  CHECK_EQ(g->st.noEncounter, 1);
}

TEST(d2menu_save_roundtrip_all) {
  if (!have(kSamples)) return;
  const char* games[] = {"0204_ace/Ace", "05yakata/05Yakata", "agasa3/agasa3", "bounty/bounty",
                         "brtsky/windows/ﾃﾞｽｸﾄｯﾌﾟ/brtsky", "dino/dino", "disaster/disaster",
                         "fant1_8/fant1_8", "fantasia/fantasia", "jewel/jewel", "keep/keep", "knuckle/knuckle",
                         "kokoro/kokoro", "mind_a/mind_a", "ring/ring", "rogues/rogues", "soul/soul",
                         "syuujin/syuujin", "tokiake/tokiake", "ue/ue/ue", "waste_w/waste_w", "ygk/ygk",
                         "zyuken/windows/ﾃﾞｽｸﾄｯﾌﾟ/zyuken"};
  int n = 0;
  for (const char* gname : games) {
    const std::string root = kSamples + gname;
    if (!have(root)) continue;
    const std::string tmp = farm(root, "rt");
    CHECK(!tmp.empty());
    ScriptHost h;
    h.script("");
    auto g = startGame(h, tmp);
    g->addItem(1);
    g->st.gold = 1234;
    g->switches[7] = 1;
    g->vars[3] = -2;
    g->updatePages();  // as closing the menu does before the next save
    CHECK(g->saveSlot(2));
    std::vector<uint8_t> a;
    CHECK(core::tryReadFile(tmp, "SAVE3.DAT", a));
    CHECK_EQ(a.size(), size_t(d2::kSaveSize));
    core::StartOptions opt;
    d2::Game g2(h, tmp, opt);
    CHECK(g2.loadSlot(2));
    CHECK(g2.saveSlot(3));
    std::vector<uint8_t> b;
    CHECK(core::tryReadFile(tmp, "SAVE4.DAT", b));
    if (a != b) std::fprintf(stderr, "  save mismatch: %s\n", gname);
    CHECK(a == b);
    ++n;
  }
  CHECK_EQ(n, 23);
}

// 23 §9.5 / 24 §6.8: flash = 10 x (2+1) VSYNC = 30; no flash = 3 wait units (21 VSYNC, 9 with SHIFT).
TEST(d2menu_magic_flash_frames) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  h.tail = 1000;
  core::StartOptions opt;
  d2::Game g(h, root, opt);
  d2::Magic m{};
  m.flash = 1;
  m.flashRgb[0] = 15;
  g.curPal[0][0] = 0x22;
  long c0 = h.count;
  d2::magicFlash(g, m);
  CHECK_EQ(h.count - c0, 30);
  CHECK_EQ(int(g.curPal[0][0]), 0x22);  // palette restored
  m.flash = 0;
  c0 = h.count;
  d2::magicFlash(g, m);
  CHECK_EQ(h.count - c0, 21);
  h.script("q*40");
  g.vsync();
  c0 = h.count;
  d2::magicFlash(g, m);
  CHECK_EQ(h.count - c0, 9);
}

// 23 §1.3–§1.9, §1.14: common rules (stat growth, recalc, level up/down, full heal, items).
TEST(d2menu_common_rules) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  core::StartOptions opt;
  d2::Game g(h, root, opt);
  d2::Member m{};
  m.maxLevel = 99;
  m.growthK = 10;
  m.lv1[0] = 100;
  m.growth[0] = 55;  // HP
  m.lv1[2] = 10;
  m.growth[2] = 25;  // attack
  // L <= K: b + g*(L-1)/10 ; L > K: b + g*(K-1)/10 + g*(L-K)/20 (each term truncated)
  CHECK_EQ(d2::Game::statForLevel(m, 2, 10), 10 + 25 * 9 / 10);
  CHECK_EQ(d2::Game::statForLevel(m, 2, 15), 10 + 25 * 9 / 10 + 25 * 5 / 20);
  m.growth[0] = 3000;
  CHECK_EQ(d2::Game::statForLevel(m, 0, 99), 9999);  // HP cap
  CHECK_EQ(d2::Game::statForLevel(m, 2, 99), 10 + 22 + 25 * 89 / 20);
  m.growth[2] = 3000;
  CHECK_EQ(d2::Game::statForLevel(m, 2, 99), 999);  // other caps
  m.growth[2] = 4000;  // 4000*9 = 36000 -> int16 -29536 -> /10 = -2953
  CHECK_EQ(d2::Game::statForLevel(m, 2, 11), 10 + int16_t(36000) / 10 + 4000 / 20);

  // §1.5: equipment sum is not truncated to 16 bits before the 0..999 clamp.
  d2::Member e{};
  e.level = 1;
  e.maxLevel = 99;
  e.lv1[2] = 500;
  for (auto& q : e.learned) q = 0xFF;
  for (auto& q : e.magicList) q = 0xFF;
  for (int s = 0; s < 5; ++s) {
    d2::Item& it = g.db.items[200 + s];
    it.f[3] = uint8_t(30000 & 0xFF);  // attack +30000 (ITEM.DAT 0x1A)
    it.f[4] = uint8_t(30000 >> 8);
    e.equip[s] = uint8_t(200 + s);
  }
  g.recalc(e);
  CHECK_EQ(e.base[0], 500);
  CHECK_EQ(e.stat[0], 999);  // 500 + 150000 -> 999 (a 16-bit wrap would give 0)
  g.db.items[200].f[3] = uint8_t(-600 & 0xFF);
  g.db.items[200].f[4] = uint8_t((-600 >> 8) & 0xFF);
  for (int s = 1; s < 5; ++s) e.equip[s] = 0xFF;
  g.recalc(e);
  CHECK_EQ(e.stat[0], 0);  // 500 - 600 -> 0

  // §1.6: multi-level up learns every level's magic; HP is not restored. Old builds write slot i.
  d2::Member u{};
  u.level = 1;
  u.maxLevel = 99;
  u.growthK = 99;
  u.expA = 10;
  u.expB = 10;
  u.lv1[0] = 50;
  u.growth[0] = 100;
  u.hp = 50;
  for (auto& q : u.learned) q = 0xFF;
  for (auto& q : u.magicList) q = 0xFF;
  u.magicList[0] = 7;
  u.learnLevel[0] = 2;
  u.magicList[1] = 9;
  u.learnLevel[1] = 4;
  u.magicList[2] = 7;  // duplicate of slot 0
  u.learnLevel[2] = 3;
  u.exp = uint32_t(d2::Game::expForLevel(u, 4));
  d2::Member old = u;
  g.build = d2::Build::B9710;
  CHECK(g.levelUp(u));
  CHECK_EQ(u.level, 4);
  CHECK_EQ(u.learned[0], 7);
  CHECK_EQ(u.learned[1], 9);
  CHECK_EQ(u.learned[2], 0xFF);  // 1997 builds learn into the first slot holding 7
  CHECK_EQ(u.hp, 50);
  CHECK(u.maxHp > 50);
  g.build = d2::Build::B9606;
  CHECK(g.levelUp(old));
  CHECK_EQ(old.learned[2], 7);  // 1996 builds write slot i directly
  // §1.7 level down clamps HP/MP
  u.exp = 0;
  u.hp = u.maxHp;
  g.levelDown(u);
  CHECK_EQ(u.level, 1);
  CHECK_EQ(u.hp, u.maxHp);
  CHECK_EQ(u.maxHp, 50);

  // §1.9 full heal: old builds add 999 only
  d2::Member fh{};
  fh.maxHp = 2000;
  fh.hp = 0;
  fh.maxMp = 50;
  fh.status[0] = fh.status[5] = fh.status[3] = 1;
  g.build = d2::Build::B9608;
  g.fullHeal(fh);
  CHECK_EQ(fh.hp, 1000);
  CHECK_EQ(fh.alive, 1);
  CHECK_EQ(fh.status[0], 0);
  CHECK_EQ(fh.status[5], 0);
  CHECK_EQ(fh.status[3], 1);
  CHECK_EQ(fh.mp, 50);
  g.build = d2::Build::B9710;
  g.fullHeal(fh);
  CHECK_EQ(fh.hp, 2000);

  // §1.14 items: 99 cap, equipped removal, possession check (1996-06 ignores equipment)
  std::memset(g.inventory, 0, sizeof(g.inventory));
  for (int i = 0; i < 99; ++i) CHECK(g.addItem(3));
  CHECK(!g.addItem(3));
  CHECK(g.addItem(4));
  g.moveItemToEnd(3);
  CHECK_EQ(g.inventory[0][0], 4);
  CHECK_EQ(g.inventory[1][0], 3);
  CHECK_EQ(g.inventory[1][1], 99);
  g.discardItem(4);
  CHECK_EQ(g.inventory[0][0], 3);
  CHECK_EQ(g.inventory[1][1], 0);
  g.st.partyCount = 1;
  g.st.party[0] = 0;
  g.members[0].equip[1] = 60;
  g.build = d2::Build::B9606;
  CHECK(!g.hasItem(60));
  g.build = d2::Build::B9608;
  CHECK(g.hasItem(60));
  CHECK(g.removeItem(60));
  CHECK_EQ(g.members[0].equip[1], 0xFF);
  CHECK(!g.hasItem(60));
}

TEST(d2menu_gold_window_and_party_window_draw) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root);
  g->st.gold = 1234567;
  g->goldWindow(0x1C8, 0x120);
  CHECK_EQ(g->st.gold, 999999u);
  // party window frame: outer outline color 14 at (x, y)
  g->scr.clear(3);
  d2::drawPartyWindow(*g, 0xC0, 0x30);
  CHECK_EQ(g->scr.pget(0xC0, 0x30), 14);
  CHECK_EQ(g->scr.pget(0xC0 + 399, 0x30 + g->st.partyCount * 24 + 23), 14);
  CHECK_EQ(g->scr.pget(0xC0 + 400, 0x30), 3);
}

namespace {
void pushKey(d2::Game& g, uint8_t k) {
  if (g.keyCount < 16) g.keyBuf[g.keyCount++] = k;
}
}  // namespace

TEST(d2menu_v11_list_open_clears_key_buffer) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root);
  std::memset(g->inventory, 0, sizeof(g->inventory));
  g->addItem(104);
  // a buffered Return must not select anything: the list clears the buffer when it opens
  pushKey(*g, d2::kKeyReturn);
  h.script("20 x");
  h.tail = 50;
  const long c0 = h.count;
  int r = 0;
  try {
    r = d2::itemListWindow(*g, 0xB8, 0x28);
  } catch (ScriptEnd&) {
    CHECK(!"list did not return");
  }
  CHECK_EQ(r, -1);
  CHECK(h.count - c0 >= 2);  // the opening VSYNC plus the page VSYNC
}

TEST(d2menu_v11_variable_editor_roll_keys) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root, true);
  g->vars[0] = 98;
  g->vars[1] = 0;
  g->vars[6] = 0;
  // debug menu: down x5 -> "変数" (Variables) -> (inject keys) -> cancel debug menu
  h.script("8 d d d d d z 60 x");
  h.tail = 60;
  const long base = h.count;
  h.onFrame = [&](long n) {
    if (n == base + 90) {
      // ROLL UP twice (98 -> 99, then stays at the limit), ROLL DOWN, right, ROLL UP,
      // down (cursor to var 6), ROLL DOWN at 0 (no change), Esc
      for (int k : {0x36, 0x36, 0x37, int(d2::kKeyRight), 0x36,
                    int(d2::kKeyDown), 0x37, int(d2::kKeyEsc)})
        pushKey(*g, uint8_t(k));
    }
  };
  try {
    d2::debugMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"debug menu did not return");
  }
  CHECK_EQ(g->vars[0], 98);
  CHECK_EQ(g->vars[1], 1);
  CHECK_EQ(g->vars[6], 0);
  // the cursor cell (var 6, column 25+8, row 4) was last drawn inverted before Esc; the text layer
  // is restored to the field mask on exit
  CHECK_EQ(g->scr.textAttr[24][40], core::kTextMask);
}

TEST(d2menu_v11_warp_count_clamped) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root);
  g->st.warpCount = 200;  // corrupt
  for (auto& w : g->st.warps) {
    w.map = 1;
    w.x = w.y = 1;
  }
  g->warpListPage = 5;  // last page: entries 25..34; 30.. must be empty
  g->warpListPos = 9;   // entry 34 (beyond 30): empty -> decide does nothing
  h.script("8 z 8 l l l l l z");
  h.tail = 50;
  int r = -2;
  try {
    r = d2::warpListWindow(*g, 0xC0, 0x38);
  } catch (ScriptEnd&) {
    CHECK(!"warp list did not return");
  }
  // The first decide on entry 34 (beyond the clamped count of 30) is ignored; the left keys move
  // to a non-empty entry before 30, which is then returned.
  CHECK(r >= 0 && r < 30);
}

TEST(d2menu_v11_reorder) {
  const std::string root = kSamples + "tokiake/tokiake";
  if (!have(root)) return;
  ScriptHost h;
  h.script("");
  auto g = startGame(h, root);
  g->st.noReorder = 0;
  const int a = g->st.party[0];
  const int b = a == 1 ? 2 : 1;
  g->joinParty(b);
  CHECK_EQ(g->st.partyCount, 2);
  // reorder (cmd 3) -> pick member 1 first -> final key -> close menu
  h.script("8 d d d z 8 d z 8 z 8 x");
  h.tail = 60;
  try {
    d2::fieldMenu(*g);
  } catch (ScriptEnd&) {
    CHECK(!"menu did not close");
  }
  CHECK_EQ(g->st.party[0], b);
  CHECK_EQ(g->st.party[1], a);
}
