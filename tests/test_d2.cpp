// D2 data/field acceptance checks (spec 20 / 21). Sample-based checks are skipped when the
// sample games are not present.
#include <cstring>
#include <map>
#include <string>

#include "core/text.h"
#include "core/util.h"
#include "d2/game.h"
#include "test.h"

namespace {
const std::string kSamples = "/home/claude/samples/games/";

bool have(const std::string& p) { return core::isDirectory(p); }

struct NullHost : core::Host {
  void vsync(const core::Screen&) override {}
  core::InputState input() override { return {}; }
  void sound(const core::SoundRequest&) override {}
};

std::string sj(const char* u) { return core::utf8ToSjis(u); }
}  // namespace

TEST(d2_build_detection) {
  const std::map<std::string, std::string> expect = {
      {"agasa3/agasa3", "1996-06"}, {"dino/dino", "1996-06"}, {"disaster/disaster", "1996-06"},
      {"mind_a/mind_a", "1996-06"}, {"soul/soul", "1996-06"}, {"tokiake/tokiake", "1996-06"},
      {"waste_w/waste_w", "1996-06"}, {"zyuken/windows/ﾃﾞｽｸﾄｯﾌﾟ/zyuken", "1996-06"},
      {"0204_ace/Ace", "1996-08"}, {"bounty/bounty", "1996-08"}, {"knuckle/knuckle", "1996-08"},
      {"ue/ue/ue", "1996-08"}, {"brtsky/windows/ﾃﾞｽｸﾄｯﾌﾟ/brtsky", "1996-08"}, {"ring/ring", "1997-03"},
      {"ygk/ygk", "1997-03"}, {"05yakata/05Yakata", "1997-10"}, {"fant1_8/fant1_8", "1997-10"},
      {"fantasia/fantasia", "1997-10"}, {"jewel/jewel", "1997-10"}, {"keep/keep", "1997-10"},
      {"kokoro/kokoro", "1997-10"}, {"rogues/rogues", "1997-10"}, {"syuujin/syuujin", "1997-10"},
  };
  if (!have(kSamples)) return;
  for (auto& [dir, b] : expect) {
    const std::string got = d2::buildName(d2::detectBuild(kSamples + dir));
    if (got != b) std::fprintf(stderr, "  %s: %s (expected %s)\n", dir.c_str(), got.c_str(), b.c_str());
    CHECK(got == b);
  }
  CHECK(std::string(d2::buildName(d2::detectBuild("/nonexistent"))) == "1997-10");
}

TEST(d2_all_games_load) {
  if (!have(kSamples)) return;
  const char* games[] = {"0204_ace/Ace", "05yakata/05Yakata", "agasa3/agasa3", "bounty/bounty",
                         "brtsky/windows/ﾃﾞｽｸﾄｯﾌﾟ/brtsky", "dino/dino", "disaster/disaster",
                         "fant1_8/fant1_8", "fantasia/fantasia", "jewel/jewel", "keep/keep", "knuckle/knuckle",
                         "kokoro/kokoro", "mind_a/mind_a", "ring/ring", "rogues/rogues", "soul/soul",
                         "syuujin/syuujin", "tokiake/tokiake", "ue/ue/ue", "waste_w/waste_w", "ygk/ygk",
                         "zyuken/windows/ﾃﾞｽｸﾄｯﾌﾟ/zyuken"};
  int maps = 0, scripts = 0;
  for (const char* g : games) {
    const std::string root = kSamples + g;
    d2::Database db;
    bool ok = true;
    try {
      d2::loadDatabase(root, db);
    } catch (std::exception& e) {
      std::fprintf(stderr, "  %s: %s\n", g, e.what());
      ok = false;
    }
    CHECK(ok);
    CHECK(db.title.valid);
    // every map file
    for (int m = 0; m < 1000; ++m) {
      if (!core::fileExists(root, d2::numberedName("DAT_MAP/MAP", m, ".DAT"))) continue;
      auto md = std::make_unique<d2::MapData>();
      d2::loadMap(root, m, *md, db.state0, 0);
      ++maps;
      const int n = md->hdr.size;
      CHECK(n == 32 || n == 48 || n == 64 || n == 80 || n == 96 || n == 112 || n == 128 || n == 144 || n == 160);
      for (int i = 0; i < d2::kMaxEvents; ++i) {
        if (!md->events[i]) continue;
        CHECK_EQ(md->events[i]->selfNo, i);
        for (int p = 0; p < 4; ++p) {
          uint16_t len = 0;
          const uint8_t* s = md->script(i, p, &len);
          CHECK(s != nullptr);
          if (s && len) {
            CHECK_EQ(s[len - 1], 0);
            ++scripts;
          }
        }
      }
      // graphics referenced by the map
      d2::PatternSet ps;
      d2::loadPatterns(root, d2::numberedName("ECD_MCP/MCP", md->hdr.chipset, ".ECD"), 160, ps);
      d2::ChipAttr ca;
      d2::loadChipAttr(root, md->hdr.chipset, ca);
    }
    for (int k = 0; k < 256; ++k) {
      d2::EnemyImage ei;
      d2::loadEnemyImage(root, d2::numberedName("EGD_ENS/ENS", k, ".EGD"), ei);
      d2::loadEnemyImage(root, d2::numberedName("EGD_ENL/ENL", k, ".EGD"), ei);
      d2::BattleBg bg;
      d2::loadBattleBg(root, d2::numberedName("EBD_BKG/BKG", k, ".EBD"), bg);
    }
  }
  std::printf("  maps=%d scripts=%d\n", maps, scripts);
  CHECK(maps == 3176);
}

TEST(d2_player_dat_ring) {
  const std::string root = kSamples + "ring/ring";
  if (!have(root)) return;
  d2::Database db;
  d2::loadDatabase(root, db);
  const d2::Member& m = db.members0[0];
  CHECK(core::trimName(reinterpret_cast<const uint8_t*>(m.name), 20) == sj("マサル"));
  CHECK_EQ(m.level, 5);
  CHECK_EQ(m.hp, 348);
  CHECK_EQ(m.maxHp, 348);
  CHECK_EQ(m.chrFile, 16);
  CHECK_EQ(m.chrPat, 0);
  CHECK_EQ(db.state0.startMap, 1);
  CHECK_EQ(db.state0.x, 18);
  CHECK_EQ(db.state0.y, 13);
  CHECK_EQ(db.state0.partyCount, 2);
  CHECK_EQ(db.state0.party[0], 0);
  CHECK_EQ(db.state0.party[1], 1);
  CHECK(std::string(db.state0.curBgm) == "EVENT01");
  CHECK_EQ(db.state0.loadedMap, 0xFFFF);
  CHECK_EQ(db.state0.dir, 2);
  // experience: ring member 0 (A=3,B=4,C=0) E(6)=195
  CHECK_EQ(d2::Game::expForLevel(m, 6), 195);
  CHECK_EQ(m.nextExp, 195u);
  d2::Member t = m;
  t.expA = 8;
  t.expB = 8;
  t.expC = 0;
  CHECK_EQ(d2::Game::expForLevel(t, 2), 12);
  CHECK_EQ(d2::Game::statForLevel(m, 0, m.level), 348);
}

TEST(d2_player_dat_mind_a) {
  const std::string root = kSamples + "mind_a/mind_a";
  if (!have(root)) return;
  d2::Database db;
  d2::loadDatabase(root, db);
  CHECK_EQ(db.state0.startMap, 15);
}

TEST(d2_item_magic_enemy_word_bounty) {
  const std::string root = kSamples + "bounty/bounty";
  if (!have(root)) return;
  d2::Database db;
  d2::loadDatabase(root, db);
  const d2::Magic& mg = db.magics[0];
  CHECK(core::rawName(reinterpret_cast<const uint8_t*>(mg.name), 17) == sj("応急手当"));
  CHECK_EQ(mg.target, 0);
  CHECK(mg.hasEffect(0));
  CHECK_EQ(mg.power, 10);
  CHECK_EQ(mg.spiCoef, 1);
  CHECK_EQ(mg.variance, 5);
  CHECK_EQ(mg.success, 100);
  CHECK_EQ(mg.mpCost, 2);
  CHECK_EQ(mg.sfx, 45);
  CHECK_EQ(db.enemies[0].image, 11);
  CHECK_EQ(db.enemies[0].maxHp, 25u);
  CHECK_EQ(db.enemies[0].mp, 9999);
  CHECK(core::rawName(reinterpret_cast<const uint8_t*>(db.enemies[0].name), 17) == sj("バウンティLV1"));
  int cnt = 0;
  for (int k = 0; k < 5; ++k)
    if (db.groups[0].enemies[k] != 0xFF) {
      CHECK_EQ(db.groups[0].enemies[k], 0);
      ++cnt;
    }
  CHECK_EQ(cnt, 1);
  CHECK(db.wordAt(0x6F) == " Cr");
  CHECK(db.wordAt(0x4A0) == sj("敵と遭遇した！"));
  const std::string w = db.wordAt(0x758);
  CHECK(w.size() > 29);
  CHECK(w.find(sj("ウチじゃ買い取れないね。")) != std::string::npos);
  CHECK_EQ(db.items[255].hitRate(), 95);
}

TEST(d2_map_ring_002) {
  const std::string root = kSamples + "ring/ring";
  if (!have(root)) return;
  d2::Database db;
  d2::loadDatabase(root, db);
  auto md = std::make_unique<d2::MapData>();
  d2::loadMap(root, 2, *md, db.state0, 0);
  CHECK_EQ(md->hdr.size, 32);
  CHECK_EQ(md->hdr.chipset, 1);
  int n = 0;
  for (auto* e : md->events) n += e ? 1 : 0;
  CHECK_EQ(n, 6);
  uint16_t len = 0;
  const uint8_t* s = md->script(0, 0, &len);
  CHECK(s != nullptr);
  CHECK_EQ(md->events[0]->pages[0].script, 550);
  CHECK_EQ(len, 810);
}

TEST(d2_chip_attr_default) {
  d2::ChipAttr ca;
  d2::loadChipAttr("/nonexistent", 0, ca);
  for (int i = 0; i < 256; ++i) {
    CHECK_EQ(ca.attr[i] & 0x7F, 0x20);
    const bool ship = i < 4 || i >= 0xC0;
    CHECK_EQ((ca.attr[i] & 0x80) != 0, ship);
  }
}

TEST(d2_save_roundtrip_05yakata) {
  const std::string root = kSamples + "05yakata/05Yakata";
  if (!have(root)) return;
  NullHost host;
  core::StartOptions opt;
  auto g = std::make_unique<d2::Game>(host, root, opt);
  std::vector<uint8_t> orig;
  CHECK(core::tryReadFile(root, "SAVE1.DAT", orig));
  CHECK_EQ(orig.size(), size_t(d2::kSaveSize));
  CHECK(g->loadSlot(0));
  CHECK_EQ(g->st.curMap, 2);
  CHECK_EQ(g->st.x, 9);
  CHECK_EQ(g->st.y, 14);
  auto s1 = g->buildSave();
  CHECK_EQ(s1.size(), size_t(d2::kSaveSize));
  // our save, then load it into a fresh game and save again: byte identical
  const std::string tmp = "/tmp/claude-0/d2_save_test";
  {
    std::string cmd = "rm -rf " + tmp + " && mkdir -p " + tmp + " && cp -r '" + root + "'/. " + tmp + "/";
    CHECK(std::system(cmd.c_str()) == 0);
  }
  auto g2 = std::make_unique<d2::Game>(host, tmp, opt);
  CHECK(g2->loadSlot(0));
  CHECK(g2->saveSlot(3));
  auto g3 = std::make_unique<d2::Game>(host, tmp, opt);
  CHECK(g3->loadSlot(3));
  auto s3 = g3->buildSave();
  std::vector<uint8_t> written;
  CHECK(core::tryReadFile(tmp, "SAVE4.DAT", written));
  CHECK(written == s3);
  // compare with the original: inventory, switches, variables, map version are unchanged;
  // party stats are recalculated from PLAYER.DAT on load (spec 20 §9.3 step 8) and the sample's
  // PLAYER.DAT growth data was edited after that save, so member base/stat values may differ.
  CHECK(std::memcmp(s1.data() + 0x0668, orig.data() + 0x0668, 0x0D22 - 0x0668) == 0);
  CHECK(std::memcmp(s1.data() + 0x03C0 + 0x30, orig.data() + 0x03C0 + 0x30, 0x2A8 - 0x30) == 0);
}

TEST(d2_rules_inventory) {
  const std::string root = kSamples + "ring/ring";
  if (!have(root)) return;
  NullHost host;
  core::StartOptions opt;
  auto g = std::make_unique<d2::Game>(host, root, opt);
  std::memset(g->inventory, 0, sizeof(g->inventory));
  CHECK(g->addItem(5));
  CHECK(g->addItem(7));
  CHECK(g->addItem(5));
  CHECK_EQ(g->itemCount(5), 2);
  CHECK(!g->addItem(0xFF));
  CHECK(g->removeItem(7));
  CHECK_EQ(g->inventory[1][1], 0);
  CHECK_EQ(g->inventory[1][0], 0xFF);
  g->st.gold = 999990;
  g->addGold(100);
  CHECK_EQ(g->st.gold, 999999u);
  g->subGold(1000000);
  CHECK_EQ(g->st.gold, 0u);
}

TEST(d2_tone_gray) {
  const std::string root = kSamples + "ring/ring";
  if (!have(root)) return;
  NullHost host;
  core::StartOptions opt;
  auto g = std::make_unique<d2::Game>(host, root, opt);
  for (int i = 0; i < 16; ++i) {
    g->basePal[i][0] = uint8_t(i);
    g->basePal[i][1] = uint8_t(15 - i);
    g->basePal[i][2] = uint8_t(i / 2);
  }
  g->toneChange(8, -1);
  for (int i = 0; i < 16; ++i) {
    const int gray = std::min(15, (i + 15 - i + i / 2) / 3 + 3);
    CHECK_EQ(g->curPal[i][0], gray * 17);
    CHECK_EQ(g->curPal[i][2], gray * 17);
  }
  CHECK_EQ(g->st.tone, 9);
  // blackout timing: 1 + 17*s frames
  const uint32_t f0 = g->frameCounter;
  g->blackOut(1);
  CHECK_EQ(g->frameCounter - f0, 18u);
  CHECK_EQ(g->tone, 0);
}
