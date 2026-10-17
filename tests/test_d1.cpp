// D1 acceptance checks (spec 10, 11, 12).
#include <cstring>
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

std::string sample(const char* rel) { return std::string("/home/claude/samples/") + rel; }

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
