// D2 menus / shops / inns / field item & magic use (spec 23) acceptance checks.
#include <unistd.h>

#include <cstdlib>
#include <cstring>
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
const std::string kSamples = "/home/claude/samples/games/";
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
  void vsync(const core::Screen&) override {
    ++count;
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

std::string sj(const char* u) { return core::utf8ToSjis(u); }

// Symlink farm of a game root so tests can write SAVEn.DAT without touching the sample.
std::string farm(const std::string& root, const std::string& name) {
  const std::string tmp = "/tmp/claude-0/d2menu_" + name;
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
  g->addItem(13);  // 片刃の剣, price 0
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
  g->addItem(104);  // 傷薬: heal 25, consumable
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
  // discard: item 1 -> 捨てる -> 捨てる
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
  g->addItem(2);   // 木刀 +7 atk
  // menu: equip (cmd 2) -> member 0 -> slot 0 -> candidate 0 (木刀) -> cancel slot -> cancel menu
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
