// D2 battle acceptance checks (spec 24). Formula tables, RNG draw counts and a deterministic
// end-to-end battle with a scripted host. Sample-based checks are skipped without samples.
#include <cstring>
#include <string>

#include "core/text.h"
#include "core/util.h"
#include "d2/battle.h"
#include "d2/game.h"
#include "test.h"

using namespace d2;
using namespace d2::bt;

namespace {
const std::string kRing = "/home/claude/samples/games/ring/ring";
const std::string kDino = "/home/claude/samples/games/dino/dino";

// Number of draws an operation consumed, measured by replaying the generator.
int drawsBetween(uint32_t before, uint32_t after) {
  core::Rng r;
  r.seed(before);
  for (int n = 0; n < 1000; ++n) {
    if (r.state == after) return n;
    r.next();
  }
  return -1;
}

Magic magic(std::initializer_list<int> bits) {
  Magic m;
  std::memset(&m, 0, sizeof(m));
  for (int b : bits) m.effect[b / 8] |= uint8_t(0x80 >> (b % 8));
  return m;
}

Battler fighter(int atk, int def, int spi, int spd, int hp = 100) {
  Battler b;
  b.used = b.alive = true;
  b.base[0] = b.cur[0] = atk;
  b.base[1] = b.cur[1] = def;
  b.base[2] = b.cur[2] = spi;
  b.base[3] = b.cur[3] = spd;
  b.hp = b.maxHp = hp;
  return b;
}

// Host that presses "ok" for 2 frames every 8 frames (enough to drive menus and key waits).
struct MashHost : core::Host {
  long frame = 0;
  void vsync(const core::Screen&) override {
    if (++frame > 200000) throw core::QuitGame();
  }
  core::InputState input() override {
    core::InputState s;
    s.ok = (frame % 8) < 2;
    return s;
  }
  void sound(const core::SoundRequest&) override {}
};
}  // namespace

TEST(d2bat_variance) {
  core::Rng r;  // seed 1: 346, 130, ...
  CHECK_EQ(variance(r, 100, 3), 108);  // 100+346%3=101, t=25, +12-130%25=+7
  CHECK_EQ(drawsBetween(1, r.state), 2);
  core::Rng r2;
  const uint32_t s0 = r2.state;
  CHECK_EQ(variance(r2, 50, 0), 50);  // p<=0: no draw
  CHECK_EQ(drawsBetween(s0, r2.state), 0);
  CHECK_EQ(variance(r2, 4, 3), 5);  // t = 5*3/12 = 1: one draw only
  CHECK_EQ(drawsBetween(s0, r2.state), 1);
}

TEST(d2bat_resist_tables) {
  CHECK_EQ(statusRate(100, 0), 100);
  CHECK_EQ(statusRate(64, 0), 90);
  CHECK_EQ(statusRate(32, 0), 60);
  CHECK_EQ(statusRate(0, 0), 0);
  CHECK_EQ(statusRate(0, 3), 20);
  CHECK_EQ(statusRate(64, 1), 60);
  CHECK_EQ(statusRate(128, 2), 0);
  CHECK_EQ(statusRate(64, 3), 110);
  CHECK_EQ(lowerRate(0), 90);
  CHECK_EQ(lowerRate(1), 60);
  CHECK_EQ(lowerRate(2), 0);
  CHECK_EQ(lowerRate(3), 100);
  CHECK_EQ(resistHpMp(101, 1), 50);
  CHECK_EQ(resistHpMp(101, 2), 0);
  CHECK_EQ(resistHpMp(101, 3), 202);
  bool el[6] = {true, false, true, false, false, false};
  uint8_t res[20] = {};
  CHECK_EQ(elementMultiplier(el, res), 64);
  res[14] = 3;
  CHECK_EQ(elementMultiplier(el, res), 128);
  res[16] = 1;
  CHECK_EQ(elementMultiplier(el, res), 64);  // x2 then /2
  res[16] = 2;
  CHECK_EQ(elementMultiplier(el, res), 0);
  CHECK_EQ(applyMultiplier(81, 128), 162);
  CHECK_EQ(applyMultiplier(81, 32), 40);
  CHECK_EQ(applyMultiplier(81, 64), 81);
}

TEST(d2bat_magic_damage) {
  Magic m = magic({0});
  m.power = 50;
  m.atkCoef = 10;
  m.variance = 0;
  Battler c = fighter(100, 0, 0, 0), t = fighter(0, 40, 0, 0);
  core::Rng r;
  // 50 + 100*10/10 - 40*10/20 = 130, variance 0 -> no draw
  CHECK_EQ(magicDamage(r, m, c, t, 64), 130);
  CHECK_EQ(r.state, 1u);
  CHECK_EQ(magicDamage(r, m, c, t, 128), 260);
  CHECK_EQ(magicDamage(r, m, c, t, 32), 65);
  CHECK_EQ(magicDamage(r, m, c, t, 0), 0);
  CHECK_EQ(r.state, 1u);
  // 16-bit truncation of coefficient products: 999*255 = 254745 -> -7399 -> /10 = -739
  Battler big = fighter(999, 0, 0, 0);
  Magic m2 = magic({0});
  m2.power = 1000;
  m2.atkCoef = 255;
  Battler t0 = fighter(0, 0, 0, 0);
  CHECK_EQ(magicDamage(r, m2, big, t0, 64), 1000 - 739);
  // defense ignore (bit 21) and spirit coefficient
  Magic m3 = magic({0, 21});
  m3.power = 10;
  m3.spiCoef = 20;
  Battler cs = fighter(0, 0, 50, 0), ts = fighter(0, 999, 400, 0);
  CHECK_EQ(magicDamage(r, m3, cs, ts, 64), 10 + 50 * 20 / 10);
  Magic m4 = magic({0});
  m4.power = 10;
  m4.spiCoef = 20;
  {
    const uint32_t before = r.state;
    const int64_t v = magicDamage(r, m4, cs, ts, 64);  // 10+100-200 = -90 -> d<2 -> rng%2
    CHECK(v == 0 || v == 1);
    CHECK_EQ(drawsBetween(before, r.state), 1);
  }
  // self-destruct uses caster HP * a / 4
  Magic m5 = magic({0, 20, 21});
  m5.power = 0;
  m5.atkCoef = 2;
  Battler hpC = fighter(0, 0, 0, 0, 300);
  CHECK_EQ(magicDamage(r, m5, hpC, t0, 64), 150);
}

TEST(d2bat_small_damage_draws_once) {
  Magic m = magic({0});
  m.power = 1;
  Battler c = fighter(0, 0, 0, 0), t = fighter(0, 0, 0, 0);
  core::Rng r;
  const int64_t v = magicDamage(r, m, c, t, 64);
  CHECK_EQ(v, 346 % 2);
  CHECK_EQ(drawsBetween(1, r.state), 1);
  core::Rng r2;
  CHECK_EQ(healAmount(r2, m, c), 346 % 2);
}

TEST(d2bat_heal_amount) {
  Magic m = magic({0});
  m.power = 30;
  m.spiCoef = 10;
  m.variance = 0;
  Battler c = fighter(0, 0, 25, 0);
  core::Rng r;
  CHECK_EQ(healAmount(r, m, c), 30 + 25);
}

TEST(d2bat_eval_vs_party) {
  Battler p = fighter(10, 10, 10, 10);
  CHECK_EQ(evalVsParty(magic({0}), p), 100);
  p.mp = 0;
  CHECK_EQ(evalVsParty(magic({1}), p), 1);  // MP 0: falls through to "any bit" -> 1
  p.mp = 5;
  CHECK_EQ(evalVsParty(magic({1}), p), 100);
  CHECK_EQ(evalVsParty(magic({4}), p), 80);  // silence not set
  p.status[2] = 1;
  CHECK_EQ(evalVsParty(magic({4}), p), 1);
  CHECK_EQ(evalVsParty(magic({8}), p), 80);
  CHECK_EQ(evalVsParty(magic({10}), p), 100);  // atk 10 > 10/2
  p.cur[0] = 5;
  CHECK_EQ(evalVsParty(magic({10}), p), 1);
  CHECK_EQ(evalVsParty(magic({}), p), 60);
  CHECK_EQ(evalVsParty(magic({22}), p), 60);
  p.alive = false;
  CHECK_EQ(evalVsParty(magic({0}), p), 0);
}

TEST(d2bat_eval_self) {
  Battler c = fighter(0, 0, 0, 0, 100);
  Battler e = fighter(0, 0, 0, 0, 100);
  e.hp = 20;
  CHECK_EQ(evalSelf(magic({0}), c, e, false), 120);
  e.hp = 40;
  CHECK_EQ(evalSelf(magic({0}), c, e, false), 80);
  e.hp = 74;  // 74/3 = 24 < 25
  CHECK_EQ(evalSelf(magic({0}), c, e, false), 40);
  e.hp = 75;
  CHECK_EQ(evalSelf(magic({0}), c, e, false), 1);
  e.status[4] = 1;
  CHECK_EQ(evalSelf(magic({6}), c, e, false), 4 * 20 + 80);
  CHECK_EQ(evalSelf(magic({11}), c, e, false), 100);  // def 0 < 0*2+10
  e.alive = false;
  CHECK_EQ(evalSelf(magic({8}), c, e, false), 200);
  CHECK_EQ(evalSelf(magic({0}), c, e, false), 0);
  c.flagA = true;
  e.alive = true;
  CHECK_EQ(evalSelf(magic({0}), c, e, false), 0);
  CHECK_EQ(evalSelf(magic({}), c, e, true), 60);
}

TEST(d2bat_action_order) {
  Battler s[10];
  s[0] = fighter(0, 0, 0, 10);
  s[5] = fighter(0, 0, 0, 20);
  core::Rng r;
  int order[10];
  actionOrder(r, s, order);
  // keys: slot0 11 + 346%11 = 16, slot5 21 + 130%21 = 25; empty slots draw nothing
  CHECK_EQ(order[0], 5);
  CHECK_EQ(order[1], 0);
  CHECK_EQ(drawsBetween(1, r.state), 2);
  // fainted battler with positive speed still draws; all-zero keys keep slot order
  Battler z[10];
  z[3].used = true;
  z[3].cur[3] = 0;  // fainted, speed 0 -> key 0 -> no draw
  core::Rng r2;
  actionOrder(r2, z, order);
  for (int i = 0; i < 10; ++i) CHECK_EQ(order[i], i);
  CHECK_EQ(r2.state, 1u);
}

TEST(d2bat_attack_value) {
  Battler self = fighter(100, 0, 0, 0);
  Battler e = fighter(0, 40, 0, 0, 1000);
  CHECK_EQ(attackValue(self, e), 80);
  e.hp = 50;
  CHECK_EQ(attackValue(self, e), 96);  // +80/5
  self.atkElem[0] = true;
  e.resist[14] = 2;  // immune: only halved (not zeroed) for the AI value
  e.hp = 1000;
  CHECK_EQ(attackValue(self, e), 40);
  e.alive = false;
  CHECK_EQ(attackValue(self, e), 0);
}

TEST(d2bat_init_from_samples) {
  if (!core::isDirectory(kRing)) return;
  MashHost h;
  core::StartOptions opt;
  Game g(h, kRing, opt);
  // Ring enemy 1 "ディング": transform at <=150 into 2; instant death immune (resist 8 = 2)
  Battler b;
  initEnemyBattler(b, g.db.enemies[1], 1);
  CHECK_EQ(b.maxHp, 300);
  CHECK_EQ(b.transformHp, 150);
  CHECK_EQ(b.transformTo, 2);
  CHECK_EQ(b.resist[8], 2);
  CHECK_EQ(b.resist[7], 1);
  CHECK_EQ(b.hitRate, 95);
  CHECK_EQ(b.critRate, 5);
  CHECK_EQ(b.listCount, 5);  // kinds 0,1,0,1,21,21,1,21 -> 0,1,0,1,1
  // enemy 3: MP 9999 (infinite)
  Battler b3;
  initEnemyBattler(b3, g.db.enemies[3], 3);
  CHECK_EQ(b3.mp, 9999);
  // party: no weapon -> item 255 (hit 95)
  Battler p;
  Member m = g.members[g.st.party[0]];
  m.equip[0] = 0xFF;
  initPartyBattler(p, m, g.st.party[0], g.db.items);
  CHECK_EQ(p.hitRate, 95);
  CHECK_EQ(p.critRate, 5);
  CHECK(core::sjisToUtf8(p.name) == "マサル");
}

TEST(d2bat_full_battle_deterministic) {
  if (!core::isDirectory(kDino)) return;
  auto runOnce = [](uint32_t seed, uint32_t* rngOut, int16_t* hpOut, uint32_t* goldOut) {
    MashHost h;
    core::StartOptions opt;
    Game g(h, kDino, opt);
    g.rng.seed(seed);
    g.st.battleBg = 0;
    const int r = battle(g, 1, 0);
    *rngOut = g.rng.state;
    *hpOut = g.members[g.st.party[0]].hp;
    *goldOut = g.st.gold;
    return r;
  };
  uint32_t r1, r2, r3, g1, g2, g3;
  int16_t h1, h2, h3;
  const int a = runOnce(1, &r1, &h1, &g1);
  const int b = runOnce(1, &r2, &h2, &g2);
  CHECK(a >= 1 && a <= 3);
  CHECK_EQ(a, b);
  CHECK_EQ(r1, r2);
  CHECK_EQ(h1, h2);
  CHECK_EQ(g1, g2);
  runOnce(7, &r3, &h3, &g3);
  CHECK(r3 != r1);
  if (a == 1) CHECK(g1 > 0);
}

// ---- spec v1.1 answers ----

TEST(d2bat_class_view_reads_rows_of_class_width) {  // D2B-Q4 (24 §12.3)
  EnemyImage small;
  small.valid = true;
  small.w = 80;
  small.h = 96;
  small.mask.assign(960, 0);
  small.planes.assign(4 * 960, 0);
  for (int i = 0; i < 960; ++i) {
    small.mask[i] = uint8_t(i & 0xFF);
    small.planes[i] = 1;            // B
    small.planes[3 * 960 + i] = 9;  // I
  }
  std::vector<uint8_t> m, p;
  classView(small, 0, m, p);  // same size: identity
  CHECK_EQ(m.size(), size_t(960));
  CHECK_EQ(m[959], 959 & 0xFF);
  CHECK_EQ(p[3 * 960 + 5], 9);
  classView(small, 1, m, p);  // large class: 20-byte rows, 960 bytes -> 48 rows, rest transparent
  const size_t n = 20 * 128;
  CHECK_EQ(m.size(), n);
  CHECK_EQ(p.size(), 4 * n);
  CHECK_EQ(m[20], 20);        // row 1 col 0 = source byte 20
  CHECK_EQ(m[959], 959 & 0xFF);
  CHECK_EQ(m[960], 0);        // past the plane: 0
  CHECK_EQ(p[0], 1);
  CHECK_EQ(p[960], 0);
  CHECK_EQ(p[3 * n + 100], 9);
  CHECK_EQ(p[3 * n + 960], 0);
  // large EGD drawn as small: first 960 bytes of each plane in 10-byte rows
  EnemyImage large;
  large.valid = true;
  large.w = 160;
  large.h = 128;
  large.mask.assign(2560, 7);
  large.planes.assign(4 * 2560, 0);
  large.planes[2560] = 5;  // R plane byte 0
  classView(large, 0, m, p);
  CHECK_EQ(m.size(), size_t(960));
  CHECK_EQ(m[959], 7);
  CHECK_EQ(p[960], 5);
  // missing EGD: all planes empty
  classView(EnemyImage(), 1, m, p);
  bool allZero = true;
  for (auto v : m) allZero = allZero && v == 0;
  for (auto v : p) allZero = allZero && v == 0;
  CHECK(allZero);
}

TEST(d2bat_variance_can_be_negative) {  // D2B-Q5 (24 §12.16 v1.1): no lower clamp
  bool found = false;
  for (uint32_t seed = 1; seed < 5000 && !found; ++seed) {
    core::Rng r;
    r.seed(seed);
    const int64_t v = variance(r, 48, 25);
    core::Rng q;
    q.seed(seed);
    int64_t e = 48 + q.next() % 3;
    const int64_t t = e * 25 / 12;
    e += t / 2 - q.next() % t;
    CHECK_EQ(v, e);
    if (v < 0) found = true;
  }
  CHECK(found);
}

TEST(d2bat_empty_party_slot) {  // D2B-Q9 (24 §19.17)
  if (!core::isDirectory(kDino)) return;
  MashHost h;
  core::StartOptions opt;
  Game g(h, kDino, opt);
  g.rng.seed(3);
  g.st.battleBg = 0;
  const int c0 = g.st.party[0];
  g.st.partyCount = 2;
  g.st.party[1] = 12;  // corrupt member number -> empty slot, still counted
  Member before[12];
  std::memcpy(before, g.members, sizeof(before));
  const int r = battle(g, 1, 0);
  CHECK(r >= 1 && r <= 3);
  for (int c = 0; c < 12; ++c)
    if (c != c0) CHECK(std::memcmp(&before[c], &g.members[c], sizeof(Member)) == 0);
}
