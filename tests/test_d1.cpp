// D1 acceptance checks (spec 10, 11, 12).
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/util.h"
#include "d1/d1_data.h"
#include "d1/d1_game.h"
#include "test.h"

namespace {

struct NullHost : core::Host {
  void vsync(const core::Screen&) override {}
  core::InputState input() override { return core::InputState(); }
  void sound(const core::SoundRequest&) override {}
};

const char* kD1Games[] = {
    "games/agasa/agasa",   "games/azusa/windows/ﾃﾞｽｸﾄｯﾌﾟ/azusa", "games/clock/clock", "games/come/come",
    "games/corpse/corpse", "games/daiti/daiti", "games/dm/dm", "games/em/em", "games/illusion/illusion",
    "games/lost/LostSheep", "games/nuigl/nuigl", "games/social/social", "d1/cook/COOK", "d1/earth/EARTH",
    "d1/haru/春の嵐", "d1/kaze/風薫るとき", "d1/maho/MAHO", "d1/night/NIGHT", "d1/olsay/OLSAY", "d1/wicca/WICCA"};

std::string sample(const char* rel) { return t::samples() + rel; }

}  // namespace

TEST(d1_exp_table) {
  const int expect[] = {10, 15, 22, 33, 49, 73, 109, 163, 244, 366, 549, 823, 1234, 1851, 2776, 4164, 6246, 9369, 14053, 22168};
  for (int l = 1; l <= 20; ++l) CHECK_EQ(d1::expNeeded(l), expect[l - 1]);
  CHECK_EQ(d1::expNeeded(21), 32168);
  CHECK_EQ(d1::expNeeded(99), 812168);
}

TEST(d1_signed_arg) {
  const uint8_t a[] = {0x88, 0x93};
  bool neg;
  CHECK_EQ(d1::signedArg(a, &neg), 5000);
  CHECK(neg);
  const uint8_t b[] = {0x00, 0x80};
  CHECK_EQ(d1::signedArg(b, &neg), 0);
  CHECK(neg);
}

TEST(d1_load_all_and_events) {
  long totalEvents = 0;
  int badPages = 0;
  for (const char* g : kD1Games) {
    d1::Data db;
    try {
      db.load(sample(g));
    } catch (std::exception& e) {
      std::fprintf(stderr, "  load %s: %s\n", g, e.what());
      CHECK(false);
      continue;
    }
    for (int b = 0; b < 16; ++b) {
      const auto& buf = db.ivents[b];
      int e = 0;
      while (!d1::eventEnd(buf, e)) {
        ++totalEvents;
        d1::PageHdr pg[4];
        d1::eventPages(buf, e, pg);
        for (int k = 0; k < 4; ++k) {
          int pc = pg[k].start + 10;
          while (pc + 1 < int(buf.size()) && !(buf[pc] == 0xFF && buf[pc + 1] == 0xFF)) pc += d1::cmdLen(buf, pc);
          const int expectEnd = k < 3 ? pg[k + 1].start : d1::nextEvent(buf, e);
          if (pc != expectEnd) ++badPages;
        }
        e = d1::nextEvent(buf, e);
      }
      // file must end at the end mark
      CHECK(e + 4 == int(buf.size()));
    }
    for (int p = 0; p < 256; ++p)
      for (int k = 0; k < 4; ++k) CHECK(db.patChips[p][k] < 512);
  }
  CHECK_EQ(totalEvents, 73940L);
  CHECK_EQ(badPages, 0);
}

TEST(d1_agasa_start_info) {
  d1::Data db;
  db.load(sample("games/agasa/agasa"));
  CHECK_EQ(db.start.screen, 80);
  CHECK_EQ(db.start.pos % 20, 3);
  CHECK_EQ(db.start.pos / 20, 1);
  CHECK_EQ(db.start.party[0], 4);
  CHECK_EQ(db.start.shipScreen, 705);
  CHECK_EQ(db.start.shipPos, 146);
  CHECK_EQ(db.start.battleBgm, 26);
  CHECK_EQ(db.start.shipBgm, 8);
  CHECK_EQ(db.start.airBgm, 18);
}

TEST(d1_earth_save_roundtrip) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("d1/earth/EARTH"), opt);
  std::vector<uint8_t> raw = core::readFile(sample("d1/earth/EARTH"), "SAVEDATA.1");
  CHECK(g.applySave(raw));
  const int c = g.party[0];
  CHECK_EQ(c, 0);
  CHECK_EQ(g.heroes[c].level, 5);
  CHECK_EQ(g.heroes[c].hp, 60);
  CHECK_EQ(g.heroes[c].mp, 25);
  CHECK_EQ(g.curScreen, 193);
  CHECK_EQ(g.heroX, 9);
  CHECK_EQ(g.heroY, 5);
  CHECK_EQ(g.gold, 209L);
  CHECK_EQ(g.fieldBgm, 5);
  std::vector<uint8_t> out = g.buildSave();
  CHECK(out.size() == 270);
  CHECK(std::memcmp(out.data(), raw.data(), 270) == 0);
}

TEST(d1_variance) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.rng.seed(1);
  CHECK_EQ(g.variance(3), 3);
  CHECK_EQ(g.rng.state, 1u);  // no RNG use for v <= 3
  // 80: r = 346 % 20 = 6; second draw 130 < 16000 -> +6
  CHECK_EQ(g.variance(80), 86);
}

TEST(d1_inventory_rules) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  CHECK(g.addItem(5));
  CHECK(g.addItem(5));
  CHECK(g.addItem(7));
  CHECK_EQ(g.invId[0], 5);
  CHECK_EQ(g.invCnt[0], 2);
  g.removeItem(5, true);
  CHECK_EQ(g.invId[0], 7);
  CHECK_EQ(g.invId[1], 0x80);
  CHECK_EQ(g.invCnt[1], 0);  // count field shifted from slot 2
  // unequipping an empty slot adds item 0x80 -> first empty count increments
  CHECK(g.addItem(0x80));
  CHECK_EQ(g.invCnt[1], 1);
}

TEST(d1_level_up) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  const int c = g.party[0];
  g.heroes[c].level = 1;
  g.heroes[c].exp = 0;
  g.heroes[c].hp = 10;
  const int rec = g.heroes[c].recMaxHp;
  CHECK_EQ(g.addExp(0, 30), 3);  // 10 -> L2, 15 -> L3, remainder 5
  CHECK_EQ(g.heroes[c].exp, 5);
  CHECK_EQ(g.heroes[c].recMaxHp, rec);
  g.heroes[c].hp = 0;
  CHECK_EQ(g.addExp(0, 1000), 0);
}

// ---------------------------------------------------------------- review additions
namespace d1 {
// Friend of d1::Game (declared in d1_game.h) giving tests access to internal steps.
struct D1Test {
  static bool pageCond(const Game& g, const PageHdr& p) { return g.pageCond(p); }
  static void msgPut(Game& g, const std::string& s) { g.msgPut(s); }
  static int msgRow(const Game& g) { return g.mR_; }
  static int msgCol(const Game& g) { return g.mC_; }
  static void msgClear(Game& g) { g.msgClear(); }
  static int encCounter(const Game& g) { return g.encCounter_; }
  static void encounterCheck(Game& g, bool* b) { g.encounterCheck(b); }
  static void walkPoison(Game& g) { g.walkPoison(); }
  static void installBlock(Game& g, const std::vector<uint8_t>& buf, int heroX, int heroY) {
    g.blockBuf_ = buf;
    g.objs_[0] = Obj();
    g.objs_[0].event = -3;
    g.objs_[0].x = heroX;
    g.objs_[0].y = heroY;
    g.objs_[0].gfx = 0;
    g.setup(true);
  }
  static const std::vector<uint8_t>& block(const Game& g) { return g.blockBuf_; }
  static const Obj& obj(const Game& g, int i) { return g.objs_[i]; }
  static int objCount(const Game& g) { return g.objCount_; }
  static void frameProc(Game& g) { g.frameProc(); }
  static void runEvent(Game& g, int o) { g.runEvent(o); }
  static bool msgOpen(const Game& g) { return g.msgOpen_; }
  static void applyPal(Game& g, int mode, int off) { g.applyPal(mode, off); }
  static const core::Screen& screen(const Game& g) { return g.scr_; }
  static void waitPress(Game& g) { g.waitPress(); }
  static int magicSelect(Game& g, int c) { return g.magicSelect(c); }
};
}  // namespace d1

namespace {

// Host whose input is scripted per frame; records sound effects and an optional per-frame probe.
struct ScriptHost : core::Host {
  core::InputState held;       // keys reported on every frame
  bool toggleOk = false;       // report ok on every other frame (lets "release" waits finish)
  long frames = 0;
  std::vector<int> sfx;
  std::function<void()> probe;
  void vsync(const core::Screen&) override {
    ++frames;
    if (probe) probe();
    if (frames > 200000) throw std::runtime_error("ScriptHost: runaway");
  }
  core::InputState input() override {
    core::InputState s = held;
    if (toggleOk) s.ok = (frames & 1) != 0;
    return s;
  }
  void sound(const core::SoundRequest& r) override {
    if (r.kind == core::SoundRequest::Sfx) sfx.push_back(r.value);
  }
};

// Builds an IVENT block buffer holding one event (spec 10 §9.1): page 0 has the given header values
// and commands, pages 1-3 carry no conditions.
std::vector<uint8_t> oneEvent(int screen, int x, int y, int gfx, int move, int trig, const std::vector<uint8_t>& cmds) {
  std::vector<uint8_t> b = {0xFF, 0xFF, uint8_t(screen & 0xFF), uint8_t(screen >> 8), uint8_t(y * 20 + x), 0, 0,
                            uint8_t(gfx), uint8_t(move), uint8_t(trig)};
  b.insert(b.end(), cmds.begin(), cmds.end());
  for (int k = 1; k < 4; ++k) {
    const uint8_t hdr[] = {0xFF, 0xFF, 0x80, 0x00, 200, 0x10, 0x27, uint8_t(gfx), uint8_t(move), uint8_t(trig)};
    b.insert(b.end(), hdr, hdr + 10);
  }
  const uint8_t tail[] = {0xFF, 0xFF, 0xFF, 0xFF};
  b.insert(b.end(), tail, tail + 4);
  return b;
}

}  // namespace

// Spec 10 acceptance 4: screen 705 -> block 8, in-block screen (1,6), offset 11760.
TEST(d1_map_block_offset) {
  d1::Data db;
  db.load(sample("games/agasa/agasa"));
  const std::vector<uint8_t> raw = core::readFile(sample("games/agasa/agasa"), "MAP08.DAT");
  CHECK(raw.size() >= 15360);
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 20; ++x) CHECK_EQ(int(db.mapCell(705, x, y)), int(raw[11760 + y * 20 + x]));
}

// Spec 11 §5.5: page conditions (item held or equipped, party mask, switch, gold; no condition -> not chosen).
TEST(d1_page_conditions) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  d1::PageHdr p{0, 0x80, 0, 200, 10000, 0, 0, 0};
  CHECK(!d1::D1Test::pageCond(g, p));  // no condition set
  p.item = 9;
  CHECK(!d1::D1Test::pageCond(g, p));
  g.addItem(9);
  CHECK(d1::D1Test::pageCond(g, p));
  g.removeItem(9, true);
  g.heroes[g.party[0]].equip[2] = 9;  // equipped by a party member also counts
  CHECK(d1::D1Test::pageCond(g, p));
  p.item = 0x80;
  p.party = 1 << g.party[0];
  CHECK(d1::D1Test::pageCond(g, p));
  p.party |= 1 << ((g.party[0] + 1) & 7);
  CHECK(!d1::D1Test::pageCond(g, p));
  p.party = 0;
  p.sw = 17;
  CHECK(!d1::D1Test::pageCond(g, p));
  g.setSwitch(17, true);
  CHECK(d1::D1Test::pageCond(g, p));
  p.gold = 500;
  g.gold = 499;
  CHECK(!d1::D1Test::pageCond(g, p));  // all set conditions must hold
  g.gold = 500;
  CHECK(d1::D1Test::pageCond(g, p));
}

// Spec 11 §7.2: wrap at 40 columns, kinsoku hang, page-full wait.
TEST(d1_message_flow) {
  ScriptHost h;
  h.held.fast = true;  // key waits finish in one frame
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  d1::D1Test::msgClear(g);
  d1::D1Test::msgPut(g, std::string(40, 'A'));
  CHECK_EQ(d1::D1Test::msgCol(g), 39);
  d1::D1Test::msgPut(g, d1::S("。"));  // kinsoku: hangs at columns 40-41
  CHECK_EQ(d1::D1Test::msgRow(g), 0);
  CHECK_EQ(d1::D1Test::msgCol(g), 41);
  d1::D1Test::msgClear(g);
  d1::D1Test::msgPut(g, std::string(40, 'A') + d1::S("あ"));  // normal full-width char wraps
  CHECK_EQ(d1::D1Test::msgRow(g), 1);
  CHECK_EQ(d1::D1Test::msgCol(g), 1);
  d1::D1Test::msgPut(g, "#");
  CHECK_EQ(d1::D1Test::msgRow(g), 2);
  CHECK_EQ(d1::D1Test::msgCol(g), -1);
  d1::D1Test::msgPut(g, "#" + std::string(40, 'B'));  // last line ends at column 39
  CHECK_EQ(d1::D1Test::msgRow(g), 3);
  const long f0 = h.frames;
  d1::D1Test::msgPut(g, "C");  // page full: key wait (fast key: 1 frame) then clear
  CHECK_EQ(h.frames - f0, 1L);
  CHECK_EQ(d1::D1Test::msgRow(g), 0);
  CHECK_EQ(d1::D1Test::msgCol(g), 0);
}

// Spec 11 §6.7 / acceptance 11: counter starts at 0, first step re-rolls (seed 1 -> 346 % 16 + 16 = 26).
TEST(d1_encounter_counter) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);  // agasa: no random encounters anywhere
  g.newGame();
  g.rng.seed(1);
  bool b = true;
  d1::D1Test::encounterCheck(g, &b);
  CHECK(!b);
  CHECK_EQ(d1::D1Test::encCounter(g), 26);
  for (int i = 0; i < 26; ++i) d1::D1Test::encounterCheck(g, &b);
  CHECK_EQ(d1::D1Test::encCounter(g), 0);
  CHECK_EQ(g.rng.state, 1u * 0x015A4E35u + 1u);  // only one draw so far
  d1::D1Test::encounterCheck(g, &b);
  CHECK_EQ(d1::D1Test::encCounter(g), 130 % 16 + 16);
}

// Spec 11 §6.8 / acceptance 15: walking poison uses the recorded max HP, clamps 1..10, never kills.
TEST(d1_walk_poison) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  d1::Hero& hero = g.heroes[g.party[0]];
  hero.status = 1;
  hero.recMaxHp = 95;  // 95 / 30 = 3, regardless of the computed max HP
  hero.hp = 50;
  d1::D1Test::walkPoison(g);
  CHECK_EQ(hero.hp, 47);
  hero.recMaxHp = 900;
  d1::D1Test::walkPoison(g);
  CHECK_EQ(hero.hp, 37);
  hero.recMaxHp = 10;
  hero.hp = 1;
  d1::D1Test::walkPoison(g);
  CHECK_EQ(hero.hp, 1);
}

// Spec 11 §11.1: palette modes and offset with clamping.
TEST(d1_palette_modes) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  auto clamp = [](int v) { return v < 0 ? 0 : v > 15 ? 15 : v; };
  for (int mode = 0; mode <= 6; ++mode)
    for (int off : {-6, 0, 3}) {
      d1::D1Test::applyPal(g, mode, off);
      for (int i = 0; i < 16; ++i) {
        int r = g.db.palet[i][0], gg = g.db.palet[i][1], b = g.db.palet[i][2];
        switch (mode) {
          case 0: r = gg = b = 15 - (r + gg + b) / 3; break;
          case 1: r += 8; gg -= 8; b -= 8; break;
          case 2: r -= 6; gg -= 6; b += 6; break;
          case 3: r = gg = b = (r + gg + b) / 3 + 1; break;
          case 4: r += 4; gg += 4; b += 4; break;
          case 5: r /= 3; gg /= 3; b /= 3; break;
          default: break;
        }
        const auto& p = d1::D1Test::screen(g).pal[i];
        CHECK_EQ(int(p[0]), clamp(r - off));
        CHECK_EQ(int(p[1]), clamp(gg - off));
        CHECK_EQ(int(p[2]), clamp(b - off));
      }
    }
}

// Spec 11 §6.6 / acceptance 13: a key (kind 6, value k) opens door graphic 0x3A+k on a bump, SFX 24,
// and rewrites page 0's graphic in the block buffer.
TEST(d1_door_and_key) {
  ScriptHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  const int scr = g.curScreen;
  const std::vector<uint8_t> buf = oneEvent(scr, 5, 5, 0x3B, 0, 0, {});
  d1::D1Test::installBlock(g, buf, 4, 5);
  CHECK_EQ(d1::D1Test::objCount(g), 2);
  h.held.right = true;
  d1::D1Test::frameProc(g);  // no key: just a touch
  CHECK_EQ(d1::D1Test::obj(g, 1).gfx, 0x3B);
  g.db.items[100].kind = 6;
  g.db.items[100].value = 0;  // key for door 0x3A only
  g.addItem(100);
  d1::D1Test::frameProc(g);
  CHECK_EQ(d1::D1Test::obj(g, 1).gfx, 0x3B);
  g.db.items[101].kind = 6;
  g.db.items[101].value = 1;
  g.addItem(101);
  h.sfx.clear();
  d1::D1Test::frameProc(g);
  CHECK_EQ(d1::D1Test::obj(g, 1).gfx, 0x63);
  CHECK_EQ(int(d1::D1Test::block(g)[7]), 0x63);
  CHECK(!h.sfx.empty() && h.sfx[0] == 24);
  CHECK(g.hasItem(101));  // keys are not consumed
}

// Spec 11 §8.2 0x65 '9': erasing an already erased object still closes the message window.
TEST(d1_event_erase_twice_closes_window) {
  ScriptHost h;
  h.held.fast = true;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  const std::vector<uint8_t> cmds = {0x2A, 2, 'A', 'B', 0x65, '9', 0x2A, 2, 'C', 'D', 0x65, '9', 0x65, '8'};
  d1::D1Test::installBlock(g, oneEvent(g.curScreen, 6, 6, 5, 0, 0, cmds), 2, 2);
  bool openDuringWait = true;
  h.probe = [&]() { openDuringWait = d1::D1Test::msgOpen(g); };
  d1::D1Test::runEvent(g, 1);
  CHECK(!openDuringWait);  // last frames are the 0x65 '8' wait, after the second erase
  CHECK_EQ(d1::D1Test::obj(g, 1).x, 0xFF);
}

// Spec 12 §10.3 step 4: after more than 10 failed tries the action is "stand still" and the
// selection ends (trait 0x08 / 0x10 do not retarget). Each try draws exactly one number here.
TEST(d1_enemy_ai_retry_limit) {
  NullHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  d1::BEnemy e;
  std::memset(e.name, ' ', 16);
  e.name[16] = 0;
  e.hp = e.maxHp = 10;
  std::memset(e.magicMask, 0, 4);
  std::memset(e.act, 0, 5);
  e.act[14 >> 3] = uint8_t(0x80 >> (14 & 7));  // only "revive" (no fallen ally -> always re-roll)
  e.flags = 0x08;
  g.rng.seed(5);
  int target = 99;
  const int act = g.testEnemyDecision(e, 2, &target);
  CHECK_EQ(act, 0x39);
  CHECK_EQ(target, -1);  // left as set by the last failed lookup, not "self" (4)
  core::Rng r;
  r.seed(5);
  for (int i = 0; i < 11; ++i) r.next();
  CHECK_EQ(g.rng.state, r.state);
  // Acceptance 9: bits only at number 0 -> one mod-40 draw plus one draw per hit of number 0.
  std::memset(e.act, 0, 5);
  e.act[0] = 0x80;
  e.flags = 0;
  g.rng.seed(9);
  core::Rng q;
  q.seed(9);
  const int m = q.next() % 40;
  for (int rep = 0; rep <= m; ++rep)
    if (q.next() < 3000) break;
  q.next();  // random target selection on the party side
  CHECK_EQ(g.testEnemyDecision(e, 0, &target), -1);
  CHECK_EQ(g.rng.state, q.state);
}

namespace {
void setLoneEnemy(d1::Game& g, int group, int atk, int exp, int gold) {
  for (int i = 0; i < 5; ++i) g.db.enemies[group * 5 + i].hp = 0;
  d1::EnemyDef& e = g.db.enemies[group * 5];
  e.hp = 1;
  e.atk = atk;
  e.def = 0;
  e.agi = 0;
  e.exp = exp;
  e.gold = gold;
  std::memset(e.act, 0, 5);
  e.flags = 0;
}
}  // namespace

// Spec 12 §17.1 / acceptance 15: victory gives the total exp and gold to each living member.
TEST(d1_battle_victory_rewards) {
  ScriptHost h;
  h.held.fast = true;
  h.toggleOk = true;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  setLoneEnemy(g, 3, 0, 7, 5);
  const int c = g.party[0];
  g.heroes[c].level = 5;
  g.heroes[c].exp = 0;
  g.heroes[c].hp = g.calcMaxHp(c);
  g.gold = 100;
  CHECK_EQ(g.battle(3, true), 1);
  CHECK_EQ(g.gold, 105L);
  CHECK_EQ(g.heroes[c].exp, 7);
  CHECK_EQ(g.heroes[c].level, 5);
}

// Spec 12 §17.2 / 11 §10.2 / acceptance 18: battle wipe while the field game-over check passes.
TEST(d1_battle_wipe_vs_game_over) {
  ScriptHost h;
  h.held.fast = true;
  h.toggleOk = true;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  setLoneEnemy(g, 3, 0, 0, 0);
  g.party[0] = 0;
  g.party[1] = 1;
  g.party[2] = g.party[3] = 0xFF;
  g.heroes[0].hp = 0;
  g.heroes[0].status = 0;
  g.heroes[1].hp = g.calcMaxHp(1);
  g.heroes[1].status = 2;  // paralysed
  CHECK_EQ(g.battle(3, true), 0);
  CHECK(g.heroes[1].hp > 0);
  CHECK(!g.isGameOver());
}

// Spec 11 §1.3 (v1.1, D1-Q3): a key-press wait looks at the fast key only when it starts.
TEST(d1_wait_press_fast_checked_once) {
  ScriptHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  h.probe = [&]() {
    h.held = core::InputState();
    if (h.frames >= 1) h.held.fast = true;  // fast pressed after the wait started
    if (h.frames >= 6) h.held.ok = true;
  };
  h.frames = 0;
  d1::D1Test::waitPress(g);
  CHECK_EQ(h.frames, 6L);  // not ended by the fast key
  // fast held at the start: exactly one frame
  h.probe = nullptr;
  h.held = core::InputState();
  h.held.fast = true;
  h.frames = 0;
  d1::D1Test::waitPress(g);
  CHECK_EQ(h.frames, 1L);
}

// Spec 11 §9.5.1 step 2 (v1.1, D1-Q8): the wait after 「魔法は使えません」
// ("magic cannot be used") ignores the fast key.
TEST(d1_no_magic_wait_ignores_fast) {
  ScriptHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  const int c = g.party[0];
  std::memset(g.db.heroes[c].magicLv, 99, 32);
  g.heroes[c].level = 1;
  h.held.fast = true;
  h.probe = [&]() {
    if (h.frames >= 9) h.held.cancel = true;
  };
  h.frames = 0;
  CHECK_EQ(d1::D1Test::magicSelect(g, c), -1);
  CHECK_EQ(h.frames, 9L);
}

// Spec 11 §8.2 (v1.1, D1-Q1): 0x76/0x77 with an argument outside '0'-'7' do nothing (the message
// window stays open) and advance 2 bytes.
TEST(d1_event_move_bad_argument) {
  ScriptHost h;
  h.held.fast = true;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  const std::vector<uint8_t> cmds = {0x2A, 2, 'A', 'B', 0x77, '/', 0x76, '8', 0x77, 0x00,
                                     0x65, '8', 0x66, 5, 0};
  d1::D1Test::installBlock(g, oneEvent(g.curScreen, 6, 6, 5, 0, 0, cmds), 2, 2);
  const int heroDir = d1::D1Test::obj(g, 0).dir, objDir = d1::D1Test::obj(g, 1).dir;
  bool openDuringWait = false;
  h.probe = [&]() { openDuringWait = d1::D1Test::msgOpen(g); };
  d1::D1Test::runEvent(g, 1);
  CHECK(openDuringWait);  // the 0x65 '8' wait runs with the window still open
  CHECK(g.getSwitch(5));  // decoding continued 2 bytes per bad command
  CHECK_EQ(d1::D1Test::obj(g, 0).dir, heroDir);
  CHECK_EQ(d1::D1Test::obj(g, 1).dir, objDir);
  CHECK_EQ(d1::D1Test::obj(g, 0).x, 2);
}

// Spec 12 §1.2 (v1.1, D1-Q6): an empty party slot as the current target is never revived and its
// status / abilities are never written; result values follow the §13.4 table.
TEST(d1_battle_empty_slot_target) {
  ScriptHost h;
  core::StartOptions opt;
  d1::Game g(h, sample("games/agasa/agasa"), opt);
  g.newGame();
  g.party[0] = 0;
  g.party[1] = 0xFF;
  g.party[2] = 0xFF;
  g.party[3] = 0xFF;
  d1::Hero before[8];
  for (int i = 0; i < 8; ++i) before[i] = g.heroes[i];
  const int kItem = -2;
  CHECK_EQ(g.testBattleEffect(11, kItem, 1), 9);  // confusion of status 0 -> "混乱してしまった" (confused)
  CHECK_EQ(g.testBattleEffect(6, kItem, 1), 0);   // revive fails
  CHECK_EQ(g.testBattleEffect(7, kItem, 1), 0);   // cure fails (status reads 0)
  CHECK_EQ(g.testBattleEffect(8, kItem, 1), 1);
  CHECK_EQ(g.testBattleEffect(12, kItem, 1), 1);
  CHECK_EQ(g.testBattleEffect(14, kItem, 1), -g.db.magics[14].power);
  CHECK_EQ(g.testBattleEffect(20, kItem, 1), -g.db.magics[20].power);  // "fell"
  for (int i = 0; i < 8; ++i) {
    CHECK_EQ(g.heroes[i].status, before[i].status);
    CHECK_EQ(g.heroes[i].hp, before[i].hp);
    CHECK_EQ(g.heroes[i].mp, before[i].mp);
  }
}
