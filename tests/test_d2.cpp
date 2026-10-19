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
const std::string kSamples = (t::samples() + "games/");

bool have(const std::string& p) {
  if (core::isDirectory(p)) return true;
  t::skipped() = true;  // sample game missing: report as skipped
  return false;
}

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
  const std::string tmp = (t::tmp() + "d2_save_test");
  core::removeAll(tmp);
  CHECK(core::copyTree(root, tmp));
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

// =============================================================================================
// Synthetic field tests (spec 21): an in-memory map built on top of a sample's database.
namespace {
struct RecHost : core::Host {
  std::vector<core::InputState> script;  // input per VSYNC (empty after the end)
  size_t frame = 0;
  std::vector<std::string> played;      // PlayFile paths
  void vsync(const core::Screen&) override {}
  core::InputState input() override { return frame < script.size() ? script[frame++] : core::InputState{}; }
  void sound(const core::SoundRequest& r) override {
    if (r.kind == core::SoundRequest::PlayFile) played.push_back(r.path);
  }
};

const std::string kSynthRoot = kSamples + "ring/ring";

// n x n map of chip 4 (attribute 0x20), hero at (10,10) facing down, no events, no vehicles.
std::unique_ptr<d2::Game> synthGame(core::Host& host, d2::Build b, int n = 32, bool loop = false) {
  core::StartOptions opt;
  auto g = std::make_unique<d2::Game>(host, kSynthRoot, opt);
  g->build = b;
  d2::MapData& m = g->map;
  std::memset(&m.hdr, 0, sizeof(m.hdr));
  m.hdr.size = uint8_t(n);
  m.hdr.loop = loop ? 1 : 0;
  m.hdr.encRate = 20;
  m.hdr.outerChip = 5;
  for (int y = 0; y < d2::kMapDim; ++y)
    for (int x = 0; x < d2::kMapDim; ++x) m.grid[y][x] = (x < n && y < n) ? 4 : 5;
  std::memset(m.zones, 0xFF, sizeof(m.zones));
  std::fill(m.eventArea.begin(), m.eventArea.end(), 0);
  for (auto& e : m.events) e = nullptr;
  std::memset(g->chips.attr, 0x20, sizeof(g->chips.attr));
  for (int i = 0; i < 4; ++i) g->chips.attr[i] |= 0x80;
  std::memset(g->chips.enc, 1, sizeof(g->chips.enc));
  std::memset(g->upperMap, 0xFF, sizeof(g->upperMap));
  std::memset(g->lowerMap, 0xFF, sizeof(g->lowerMap));
  d2::StateBlock& st = g->st;
  st.curMap = st.loadedMap = 0;
  st.x = st.y = 10;
  st.dir = 2;
  st.vehicle = 0;
  st.heroSprite = 0;
  st.hidden = 0;
  st.shipMap = st.airMap = 0xFFFF;
  st.noEncounter = 0;
  st.eventRunning = 0;
  st.poison = 0;
  g->enterMap();
  return g;
}

// Event i at (x,y) with one page (no condition, graphic 0x100, empty script).
d2::EventRec* addEvent(d2::Game& g, int i, int x, int y, int moveType, int trigger, int priority) {
  const int off = 2 + i * 0x80;
  auto* e = reinterpret_cast<d2::EventRec*>(&g.map.eventArea[off]);
  std::memset(e, 0, sizeof(*e));
  e->x = uint8_t(x);
  e->y = uint8_t(y);
  e->layer = 0x20;
  e->selfNo = uint16_t(i);
  e->timer = 10;
  d2::EventPage& p = e->pages[0];
  p.gfx = 0x100;
  p.moveType = uint8_t(moveType);
  p.trigger = uint8_t(trigger);
  p.priority = uint8_t(priority);
  p.script = uint16_t(off + 0x62 + 2);
  core::wr16(&g.map.eventArea[off + 0x62], 1);  // empty script: length 1, byte 0
  g.map.events[i] = e;
  return e;
}
}  // namespace

// 21 §8.3 / acceptance 8: random NPC (type 1) stops 27..36 cycles between steps, deterministic
// for a fixed seed.
TEST(d2_npc_random_timing) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  std::vector<std::pair<int, int>> trace[2];
  for (int run = 0; run < 2; ++run) {
    auto g = synthGame(host, d2::Build::B9710, 32, true);
    g->rng.seed(7);
    d2::EventRec* e = addEvent(*g, 0, 20, 20, 1, 0, 1);
    g->updatePages();
    int lastStart = -1, starts = 0;
    int stopped = 0;
    for (int c = 0; c < 2000; ++c) {
      const uint8_t before = e->prog;
      g->npcCycle();
      if (before == 0 && e->prog == 0) ++stopped;
      if (before == 0 && e->prog == 8) {
        if (lastStart >= 0) {
          // 4 moving cycles + (36 - timer) stopped cycles, timer = rand % 10
          CHECK(c - lastStart >= 4 + 27);
          CHECK(c - lastStart <= 4 + 36);
          CHECK(stopped + 1 >= 27 && stopped + 1 <= 36);
        }
        lastStart = c;
        stopped = 0;
        ++starts;
        trace[run].push_back({e->x, e->y});
      }
    }
    CHECK(starts > 40);
  }
  CHECK(trace[0] == trace[1]);
}

// 21 §8.3: up/down round trip moves every 8 stopped cycles; both directions blocked -> timer 0.
TEST(d2_npc_round_trip) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710, 32, true);
  d2::EventRec* e = addEvent(*g, 0, 20, 20, 3, 0, 1);
  g->updatePages();
  int lastStart = -1, n = 0;
  for (int c = 0; c < 400; ++c) {
    const uint8_t before = e->prog;
    g->npcCycle();
    if (before == 0 && e->prog == 8) {
      CHECK_EQ(e->timer, 28);
      if (lastStart >= 0) CHECK_EQ(c - lastStart, 4 + 8);
      lastStart = c;
      ++n;
    }
  }
  CHECK(n >= 30);
  // block every edge of the NPC's cell
  e->prog = 0;
  std::memset(e->dirProg, 0, 4);
  g->map.grid[e->y][e->x] = 6;
  g->chips.attr[6] = 0x2F;
  e->timer = 35;
  g->npcCycle();
  CHECK_EQ(e->prog, 0);
  CHECK_EQ(e->timer, 0);
}

// 20 §10.3 C: a random NPC that fails to move keeps the tried direction on old builds only.
TEST(d2_build_c_random_fail_direction) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  for (int old = 0; old < 2; ++old) {
    auto g = synthGame(host, old ? d2::Build::B9608 : d2::Build::B9710);
    d2::EventRec* e = addEvent(*g, 0, 20, 20, 1, 0, 1);
    g->updatePages();
    g->map.grid[20][20] = 6;
    g->chips.attr[6] = 0x2F;  // all edges blocked
    // find a seed whose attempt tries a direction other than "down"
    uint32_t seed = 1;
    int tried = 2;
    for (;; ++seed) {
      core::Rng r;
      r.seed(seed);
      const int r1 = r.next();
      tried = (r1 & 3) ? (r.next() & 3) * 2 + 2 : 2;
      if (tried != 2) break;
    }
    g->rng.seed(seed);
    e->dir = 2;
    e->timer = 35;
    g->npcCycle();
    CHECK_EQ(e->prog, 0);
    CHECK_EQ(e->dir, old ? tried : 2);
  }
}

// 20 §10.3 B: sea rule for NPCs (old builds always, new builds only when chip 0 is 0x0F).
TEST(d2_build_b_sea_rule) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  struct Case {
    d2::Build b;
    uint8_t attr0;
    bool moves;
  } cases[] = {{d2::Build::B9606, 0xA0, false}, {d2::Build::B9710, 0xA0, true}, {d2::Build::B9710, 0xAF, false}};
  for (auto& c : cases) {
    auto g = synthGame(host, c.b);
    d2::EventRec* e = addEvent(*g, 0, 20, 20, 0, 0, 1);
    g->map.grid[20][20] = 0;  // sea under the NPC, land (chip 4) to the right
    g->chips.attr[0] = c.attr0;
    g->updatePages();
    CHECK_EQ(g->npcTryStep(0, 6, false), c.moves);
    CHECK_EQ(e->dir, 6);
  }
}

// 21 §8.4 step 4.4: a pass-through NPC may step onto the hero even when the hero stands on the
// parked airship; a priority-0 NPC is blocked by the hero (result 2, approaching -> pending).
TEST(d2_npc_onto_hero_on_airship) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  g->st.airMap = 0;
  g->st.airX = 10;
  g->st.airY = 10;
  addEvent(*g, 0, 11, 10, 0, 0, 1);
  addEvent(*g, 1, 9, 10, 5, 1, 0);
  g->updatePages();
  g->rng.seed(3);
  CHECK(!g->npcTryStep(1, 6, false));  // priority 0, approaching: touches the hero
  CHECK_EQ(g->st.pendingKind, 1);
  CHECK_EQ(g->st.pendingEvent, 1);
  CHECK(g->npcTryStep(0, 4, false));  // priority 1 onto the hero's cell (and the airship's)
  CHECK_EQ(g->map.events[0]->x, 10);
  // the airship elsewhere still blocks
  g->st.airX = 10;
  g->st.airY = 8;
  addEvent(*g, 3, 10, 7, 0, 0, 1);
  g->updatePages();
  CHECK(!g->npcTryStep(3, 2, false));
}

// 21 §6.1/§6.2 / acceptance 6: a step takes 4 hero cycles (2 when fast); edge bits and layers
// block; non-loop edges block; loop maps wrap.
TEST(d2_hero_step) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  const int vx0 = g->viewX;
  g->heroStep(6, false);
  CHECK_EQ(g->st.scroll, 8);
  CHECK_EQ(g->st.x, 11);
  int calls = 0;
  while (g->st.scroll) {
    g->heroProcess();
    ++calls;
  }
  CHECK_EQ(calls, 4);
  CHECK_EQ(g->viewX, vx0 + 1);
  CHECK_EQ(g->st.steps, 1);
  g->heroStep(6, true);
  calls = 0;
  while (g->st.scroll) {
    g->heroProcess();
    ++calls;
  }
  CHECK_EQ(calls, 2);
  // edge bit on the current cell (right side)
  g->map.grid[12][12] = 6;
  g->chips.attr[6] = 0x24;
  g->st.x = g->st.y = 12;
  g->enterMap();
  g->heroStep(6, false);
  CHECK_EQ(g->st.scroll, 0);
  CHECK_EQ(g->st.dir, 6);
  g->heroStep(2, false);  // other edges are open
  CHECK_EQ(g->st.scroll, 8);
  while (g->st.scroll) g->heroProcess();
  // upper-layer chip next to a lower-layer hero blocks; a connector cell lets it through
  g->map.grid[13][13] = 7;
  g->chips.attr[7] = 0x00;
  g->heroStep(6, false);
  CHECK_EQ(g->st.scroll, 0);
  g->map.grid[13][12] = 8;
  g->chips.attr[8] = 0x60;
  g->enterMap();
  CHECK_EQ(g->st.layer, 0x60);
  g->heroStep(6, false);
  CHECK_EQ(g->st.scroll, 8);
  CHECK_EQ(g->st.layer, 0x00);
  while (g->st.scroll) g->heroProcess();
  // non-loop edge
  g->st.x = 31;
  g->st.y = 5;
  g->enterMap();
  g->heroStep(6, false);
  CHECK_EQ(g->st.scroll, 0);
  // loop map wraps
  auto h = synthGame(host, d2::Build::B9710, 32, true);
  h->st.x = 31;
  h->enterMap();
  CHECK_EQ(h->viewX, 22);
  h->heroStep(6, false);
  CHECK_EQ(h->st.x, 0);
  while (h->st.scroll) h->heroProcess();
  CHECK_EQ(h->viewX, 23);
  h->st.x = 0;
  h->st.y = 0;
  h->enterMap();
  CHECK_EQ(h->viewX, 23);
  CHECK_EQ(h->viewY, 26);
}

// 21 §6.4 / acceptance 7: poison costs 1 HP per walking step (never below 1) and flashes T=160.
TEST(d2_poison_walk) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  g->st.partyCount = 2;
  g->st.party[0] = 0;
  g->st.party[1] = 1;
  g->members[0].status[0] = 1;
  g->members[0].hp = 5;
  g->members[1].status[0] = 1;
  g->members[1].hp = 1;
  g->updatePoison();
  CHECK_EQ(g->st.poison, 1);
  g->tone = 100;
  g->heroStep(2, false);
  g->heroProcess();  // 8
  g->heroProcess();  // 16
  CHECK_EQ(g->tone, 100);
  g->heroProcess();  // 24
  CHECK_EQ(g->tone, 160);
  CHECK_EQ(g->members[0].hp, 4);
  CHECK_EQ(g->members[1].hp, 1);
  g->heroProcess();  // 32: arrival
  CHECK_EQ(g->tone, 100);
  CHECK_EQ(g->st.scroll, 0);
  // fast steps never pass 24: no damage
  g->heroStep(2, true);
  while (g->st.scroll) g->heroProcess();
  CHECK_EQ(g->members[0].hp, 4);
}

// 21 §10.1 / acceptance 12: no encounter in the first 4 steps at rate 20 (no random number used),
// none when switched off, none on chips of kind 0, none on the ship's cell.
TEST(d2_encounter_rules) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  uint8_t* z = g->map.zones;  // zone (0,0): group 0 always
  core::wr16(z, 0xFFFF);
  z[16] = 0;
  g->rng.seed(1);
  for (int s = 1; s <= 4; ++s) {
    g->st.steps = uint16_t(s);
    CHECK_EQ(g->encounterCheck(), 0);
  }
  CHECK_EQ(g->rng.state, 1u);
  // step 5 at rate 20: one random number, 1/40; pick a seed that does not hit
  uint32_t seed = 1;
  for (;; ++seed) {
    core::Rng r;
    r.seed(seed);
    if (r.next() % 40 != 0) break;
  }
  g->rng.seed(seed);
  g->st.steps = 5;
  CHECK_EQ(g->encounterCheck(), 0);
  core::Rng r;
  r.seed(seed);
  r.next();
  CHECK_EQ(g->rng.state, r.state);
  // switched off / chip kind 0 / no candidates (switch condition off)
  g->map.hdr.encRate = 0;  // rate 0 would hit every step
  g->st.noEncounter = 1;
  CHECK_EQ(g->encounterCheck(), 0);
  g->st.noEncounter = 0;
  g->chips.enc[4] = 0;
  CHECK_EQ(g->encounterCheck(), 0);
  g->chips.enc[4] = 1;
  core::wr16(z, 5);
  CHECK_EQ(g->encounterCheck(), 0);
  core::wr16(z, 0xFFFF);
  // the ship on the hero's cell prevents it (steps are not reset)
  g->st.shipMap = 0;
  g->st.shipX = g->st.x;
  g->st.shipY = g->st.y;
  g->st.steps = 9;
  const uint32_t before = g->rng.state;
  CHECK_EQ(g->encounterCheck(), 0);
  CHECK_EQ(g->st.steps, 9);
  CHECK_EQ(g->rng.state, before);
}

// 21 §7.1 / acceptance 9: pages 1..3 without condition are never chosen; switch changes are
// visible immediately; §7.2 page -1 removes the event from the position maps.
TEST(d2_page_selection) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  d2::EventRec* e = addEvent(*g, 0, 12, 12, 0, 0, 0);
  for (int p = 1; p < 4; ++p) e->pages[p] = e->pages[0];
  g->updatePages();
  CHECK_EQ(g->activePage(0), 0);
  CHECK_EQ(g->lowerMap[12][12], 0);
  e->pages[2].cond = 1;
  e->pages[2].condSwitch = 7;
  CHECK_EQ(g->activePage(0), 0);
  g->switches[7] = 1;
  CHECK_EQ(g->activePage(0), 2);
  e->pages[0].cond = 1;
  e->pages[0].condSwitch = 8;
  g->switches[7] = 0;
  CHECK_EQ(g->activePage(0), -1);
  g->updatePages();
  CHECK_EQ(g->lowerMap[12][12], 0xFD);
  CHECK_EQ(e->active, 0);
  CHECK_EQ(e->page1, 0);
  g->switches[8] = 1;
  g->updatePages();
  CHECK_EQ(g->lowerMap[12][12], 0);
  CHECK_EQ(e->page1, 1);
  CHECK_EQ(e->active, 1);
}

// 20 §10.3 A / 21 §7.5: erasing an event registered in both maps (connector chip).
TEST(d2_build_a_erase_both_maps) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  for (int old = 0; old < 2; ++old) {
    auto g = synthGame(host, old ? d2::Build::B9608 : d2::Build::B9703);
    addEvent(*g, 0, 12, 12, 0, 0, 0);
    g->updatePages();
    g->upperMap[12][12] = 0;
    g->eraseEvent(0, true);
    CHECK_EQ(g->lowerMap[12][12], 0xFD);
    CHECK_EQ(g->upperMap[12][12], old ? 0 : 0xFD);
    CHECK_EQ(g->map.events[0]->page1, 9);
    g->updatePages();  // permanently erased: never re-registered
    CHECK_EQ(g->lowerMap[12][12], 0xFD);
  }
}

// 21 §9.1 / acceptance 11: boarding the ship switches to the ship BGM; leaving moves one step
// onto land and restores the BGM.
TEST(d2_ship_board_leave) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  std::memset(g->st.curBgm, 0, 9);
  std::memset(g->st.shipBgm, 0, 9);
  std::strcpy(g->st.curBgm, "FIELD");
  std::strcpy(g->st.shipBgm, "SHIP");
  g->map.grid[10][10] = 0;  // hero (and ship) on the sea, land to the right
  g->enterMap();
  g->st.shipMap = 0;
  g->st.shipX = 10;
  g->st.shipY = 10;
  CHECK_EQ(g->sameLayerValue(9, 6), d2::kShipValue);
  g->boardShip(true);
  CHECK_EQ(g->st.vehicle, 1);
  CHECK_EQ(g->st.heroSprite, 8);
  CHECK_EQ(g->st.shipMap, 0xFFFF);
  CHECK(std::string(g->st.curBgm) == "SHIP");
  CHECK(std::string(g->st.prevBgm) == "FIELD");
  CHECK(!host.played.empty() && host.played.back() == "DAT_BGM/SHIP.MML");
  // the ship cannot enter land chips
  g->heroStep(2, false);
  CHECK_EQ(g->st.scroll, 0);
  g->map.grid[11][10] = 1;
  g->heroStep(2, false);
  CHECK_EQ(g->st.scroll, 8);
  while (g->st.scroll) g->heroProcess();
  CHECK_EQ(g->st.steps, 0);  // no step count on vehicles
  g->st.y = 10;
  g->enterMap();
  g->st.dir = 6;
  CHECK(g->canDisembarkShip(6));
  g->leaveShip();
  CHECK_EQ(g->st.vehicle, 0);
  CHECK_EQ(g->st.shipMap, 0);
  CHECK_EQ(g->st.shipX, 10);
  CHECK_EQ(g->st.shipY, 10);
  CHECK_EQ(g->st.x, 11);
  CHECK_EQ(g->st.scroll, 8);
  CHECK(std::string(g->st.curBgm) == "FIELD");
  CHECK(std::string(g->st.prevBgm) == "SHIP");
  CHECK(host.played.back() == "DAT_BGM/FIELD.MML");
}

// 21 §9.2/§9.3 / acceptance 11: take-off and landing animations are 9 frames x 3 VSYNC, landing
// on a blocked cell climbs back up, the shadow shows 2 of every 3 VSYNCs.
TEST(d2_airship) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  std::memset(g->st.airBgm, 0, 9);
  std::strcpy(g->st.airBgm, "AIR");
  std::memset(g->st.curBgm, 0, 9);
  std::strcpy(g->st.curBgm, "FIELD");
  g->st.airMap = 0;
  g->st.airX = 10;
  g->st.airY = 10;
  g->st.hidden = 1;
  uint32_t f0 = g->frameCounter;
  g->takeOff();
  CHECK_EQ(g->frameCounter - f0, 24u);
  CHECK_EQ(g->st.vehicle, 2);
  CHECK_EQ(g->st.heroSprite, 0x10);
  CHECK_EQ(g->st.airMap, 0xFFFF);
  CHECK_EQ(g->st.dir, 6);
  CHECK_EQ(g->st.hidden, 0);
  CHECK(std::string(g->st.curBgm) == "AIR");
  int shown = 0;
  for (int i = 0; i < 9; ++i) {
    g->vsync();
    if (g->scr.textAttr[12][39] == core::kTextBlack) ++shown;
  }
  CHECK_EQ(shown, 6);
  // flying: fast steps, no passability checks
  g->map.grid[10][11] = 6;
  g->chips.attr[6] = 0x2F;
  g->heroStep(6, true);
  CHECK_EQ(g->st.scroll, 16);
  while (g->st.scroll) g->heroProcess();
  CHECK_EQ(g->st.x, 11);
  // landing refused on an impassable chip
  f0 = g->frameCounter;
  g->land();
  CHECK_EQ(g->frameCounter - f0, 48u);
  CHECK_EQ(g->st.vehicle, 2);
  // landing on plain ground
  g->st.x = 12;
  g->enterMap();
  g->land();
  CHECK_EQ(g->st.vehicle, 0);
  CHECK_EQ(g->st.heroSprite, 0);
  CHECK_EQ(g->st.airMap, 0);
  CHECK_EQ(g->st.airX, 12);
  CHECK_EQ(g->st.dir, 2);
  CHECK_EQ(g->st.layer, 0x20);
  CHECK(std::string(g->st.curBgm) == "FIELD");
  CHECK(g->scr.textAttr[12][39] != core::kTextBlack);
}

// 21 §5.7 / acceptance 5: the view does not stop at a non-loop map edge (outer chip shown);
// covering chips and bridges hide the hero.
TEST(d2_draw_edges_and_cover) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  // chip 5 (outer) = color 1, chip 4 = color 0, hero patterns = color 2 (opaque)
  std::fill(g->chipset.data.begin(), g->chipset.data.end(), 0);
  uint8_t* p5 = g->chipset.data.data() + 5 * d2::kPatternBytes;
  std::memset(p5 + 128, 0xFF, 128);
  for (int k = 0; k < 8; ++k) {
    std::memset(g->cache[k], 0, d2::kPatternBytes);
    std::memset(g->cache[k], 0xFF, 128);        // mask
    std::memset(g->cache[k] + 256, 0xFF, 128);  // plane 1
  }
  auto px = [&](int vx, int vy) { return g->scr.pix[(-16 + 32 * vy + 8) * core::kScreenW + 16 + 32 * vx + 8]; };
  g->st.x = 0;
  g->st.y = 0;
  g->enterMap();
  CHECK_EQ(g->viewX, -9);
  g->drawField();
  CHECK_EQ(px(8, 6), 1);   // left of the hero: outside the map
  CHECK_EQ(px(9, 5), 1);   // above
  CHECK_EQ(px(10, 6), 0);  // map
  CHECK_EQ(px(9, 6), 2);   // hero
  // loop map: the other side of the map is shown instead
  g->map.hdr.loop = 1;
  g->enterMap();
  g->drawField();
  CHECK_EQ(px(8, 6), 0);
  // covering chip hides the hero; bridge hides a lower-layer hero
  g->map.hdr.loop = 0;
  g->st.x = g->st.y = 10;
  g->enterMap();
  g->map.grid[10][10] = 6;
  g->chips.attr[6] = 0x30;
  g->drawField();
  CHECK_EQ(px(9, 6), 0);
  g->chips.attr[6] = 0x40;
  g->st.layer = 0x20;
  g->drawField();
  CHECK_EQ(px(9, 6), 0);
  g->st.layer = 0x00;
  g->drawField();
  CHECK_EQ(px(9, 6), 2);
}

// 01 §4.3 / 21 §1.3: key buffer with typematic (first repeat 28 frames after the press, then
// every 3), and direction reading with the "later direction wins" rule.
TEST(d2_key_buffer_and_direction) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  core::InputState right;
  right.right = true;
  host.script.assign(40, right);
  host.frame = 0;
  g->clearKeys();
  for (int f = 1; f <= 40; ++f) g->vsync();
  CHECK_EQ(g->keyCount, 6);  // frames 1, 28, 31, 34, 37, 40
  g->vsync();                // released
  g->clearKeys();
  // diagonal: initial memory is "horizontal" -> the vertical direction wins
  core::InputState diag;
  diag.right = diag.down = true;
  host.script.assign(1, diag);
  host.frame = 0;
  g->vsync();
  for (auto& l : g->latch) l = false;
  g->lastAxisVertical = false;
  CHECK_EQ(g->readDirection(), 2);
  g->lastAxisVertical = true;
  CHECK_EQ(g->readDirection(), 6);
  core::InputState none;
  host.script.assign(1, none);
  host.frame = 0;
  g->vsync();
  CHECK_EQ(g->readDirection(), 0);
  g->latch[4] = true;  // ok/cancel latched
  CHECK_EQ(g->readDirection(), 5);
}

// 20 §9.3 step 7 / acceptance 8: a save whose map version differs restores no event state.
TEST(d2_save_version_mismatch) {
  const std::string root = kSamples + "05yakata/05Yakata";
  if (!have(root)) return;
  RecHost host;
  core::StartOptions opt;
  const std::string tmp = (t::tmp() + "d2_save_ver_test");
  core::removeAll(tmp);
  CHECK(core::copyTree(root, tmp));
  std::vector<uint8_t> save;
  CHECK(core::tryReadFile(tmp, "SAVE1.DAT", save));
  if (save.size() != size_t(d2::kSaveSize)) return;
  d2::Database db;
  d2::loadDatabase(tmp, db);
  auto initial = std::make_unique<d2::MapData>();
  d2::loadMap(tmp, core::rd16(&save[0x03C0 + 1]), *initial, db.state0, 0);
  // original save: some NPC is not at its initial position
  auto g = std::make_unique<d2::Game>(host, tmp, opt);
  CHECK(g->loadSlot(0));
  int moved = 0;
  for (int i = 0; i < d2::kMaxEvents; ++i)
    if (initial->events[i] && g->map.events[i] &&
        (g->map.events[i]->x != initial->events[i]->x || g->map.events[i]->y != initial->events[i]->y))
      ++moved;
  CHECK(moved > 0);
  // version changed: everything at the initial positions
  core::wr16(&save[0x0D20], uint16_t(core::rd16(&save[0x0D20]) + 1));
  CHECK(core::writeFileAbs(core::resolvePath(tmp, "SAVE1.DAT"), save));
  auto h = std::make_unique<d2::Game>(host, tmp, opt);
  CHECK(h->loadSlot(0));
  for (int i = 0; i < d2::kMaxEvents; ++i)
    if (initial->events[i] && h->map.events[i]) {
      CHECK_EQ(h->map.events[i]->x, initial->events[i]->x);
      CHECK_EQ(h->map.events[i]->y, initial->events[i]->y);
    }
  // a missing slot is a failed load
  CHECK(!h->loadSlot(2));
}

// 20 §7.3: a missing map file gives the default map and keeps the previous map version;
// 20 §8.4: a truncated picture is "no image".
TEST(d2_default_map_and_short_picture) {
  if (!have(kSynthRoot)) return;
  d2::Database db;
  d2::loadDatabase(kSynthRoot, db);
  db.state0.defChr[2] = 77;
  std::memcpy(db.state0.defBattleBgm, "BATTLE\0\0", 9);
  auto m = std::make_unique<d2::MapData>();
  d2::loadMap(kSynthRoot, 999, *m, db.state0, 1234);
  CHECK(!m->fromFile);
  CHECK_EQ(m->version, 1234);
  CHECK_EQ(m->hdr.size, 32);
  CHECK_EQ(m->hdr.encRate, 20);
  CHECK_EQ(m->hdr.chr[2], 77);
  CHECK(std::string(m->hdr.battleBgm) == "BATTLE");
  CHECK_EQ(m->zones[0], 0xFF);
  const std::string tmp = (t::tmp() + "d2_short_pct");
  core::removeAll(tmp);
  CHECK(core::makeDirs(tmp + "/EBD_PCT"));
  CHECK(core::writeFileAbs(tmp + "/EBD_PCT/PCT001.EBD", std::vector<uint8_t>(128047, 0x11)));
  CHECK(core::writeFileAbs(tmp + "/EBD_PCT/PCT002.EBD", std::vector<uint8_t>(128048, 0x11)));
  d2::Picture pic;
  CHECK(!d2::loadPicture(tmp, "EBD_PCT/PCT001.EBD", pic));
  CHECK(!pic.valid);
  CHECK(d2::loadPicture(tmp, "ebd_pct/pct002.ebd", pic));
  CHECK(pic.valid);
}

// =============================================================================================
// Spec v1.1 changes (D2F-Q4, Q6, Q11, Q12, 01 §4.3 ROLL keys)
namespace {
std::string tmpGameDir(const char* name) {
  const std::string dir = std::string((t::tmp() + "d2_v11_")) + name;
  core::removeAll(dir);
  if (!core::makeDirs(dir + "/DAT_MAP") || !core::makeDirs(dir + "/EBD_BKG")) return std::string();
  return dir;
}
std::vector<uint8_t> mapFile(int n, size_t cut) {
  std::vector<uint8_t> d(0x2C + size_t(n) * n + 0x960 + 4 + 500, 0);
  d[0] = uint8_t(n);
  d[1] = 7;  // outer chip
  for (size_t i = 0; i < size_t(n) * n; ++i) d[0x2C + i] = 9;
  const size_t z = 0x2C + size_t(n) * n;
  std::memset(&d[z], 0xFF, 0x960);
  d[z] = 0x34;  // zone (0,0) condition low byte marker
  core::wr16(&d[z + 0x960], 0xBEEF);    // version
  core::wr16(&d[z + 0x962], 0);         // event area size
  std::memset(&d[z + 0x964], 0xFF, 500);
  if (cut < d.size()) d.resize(cut);
  return d;
}
}  // namespace

TEST(d2_v11_map_version_partial) {
  const std::string dir = tmpGameDir("ver");
  if (dir.empty()) return;
  d2::StateBlock st{};
  const size_t z = 0x2C + 32 * 32;
  auto m = std::make_unique<d2::MapData>();
  // cut before the version: previous version kept
  CHECK(core::writeFileAbs(dir + "/DAT_MAP/MAP000.DAT", mapFile(32, z + 0x960)));
  d2::loadMap(dir, 0, *m, st, 0x1234);
  CHECK_EQ(m->version, 0x1234);
  CHECK_EQ(m->zones[0], 0x34);
  // one version byte present: only the low byte replaced
  CHECK(core::writeFileAbs(dir + "/DAT_MAP/MAP000.DAT", mapFile(32, z + 0x961)));
  d2::loadMap(dir, 0, *m, st, 0x1234);
  CHECK_EQ(m->version, 0x12EF);
  CHECK_EQ(m->eventAreaSize, 0);
  // complete file
  CHECK(core::writeFileAbs(dir + "/DAT_MAP/MAP000.DAT", mapFile(32, size_t(-1))));
  d2::loadMap(dir, 0, *m, st, 0x1234);
  CHECK_EQ(m->version, 0xBEEF);
  // zones cut in the middle: byte-wise
  CHECK(core::writeFileAbs(dir + "/DAT_MAP/MAP000.DAT", mapFile(32, z + 1)));
  d2::loadMap(dir, 0, *m, st, 0x1234);
  CHECK_EQ(m->zones[0], 0x34);
  CHECK_EQ(m->zones[1], 0xFF);
}

TEST(d2_v11_map_size_raw_n) {
  const std::string dir = tmpGameDir("size");
  if (dir.empty()) return;
  d2::StateBlock st{};
  auto m = std::make_unique<d2::MapData>();
  // N = 200: file offsets use the raw N, memory capped to 160
  CHECK(core::writeFileAbs(dir + "/DAT_MAP/MAP000.DAT", mapFile(200, size_t(-1))));
  d2::loadMap(dir, 0, *m, st, 0);
  CHECK_EQ(m->n(), 160);
  CHECK_EQ(m->zones[0], 0x34);
  CHECK_EQ(m->version, 0xBEEF);
  CHECK_EQ(m->grid[159][159], 9);
  // N = 0: no crash, treated as size 1
  CHECK(core::writeFileAbs(dir + "/DAT_MAP/MAP000.DAT", mapFile(0, size_t(-1))));
  d2::loadMap(dir, 0, *m, st, 0);
  CHECK_EQ(m->n(), 1);
  CHECK_EQ(m->version, 0xBEEF);
  CHECK_EQ(m->grid[0][1], 7);
  // N = 48 (a normal size) unaffected
  CHECK(core::writeFileAbs(dir + "/DAT_MAP/MAP000.DAT", mapFile(48, size_t(-1))));
  d2::loadMap(dir, 0, *m, st, 0);
  CHECK_EQ(m->n(), 48);
  CHECK_EQ(m->grid[47][47], 9);
  CHECK_EQ(m->grid[48][0], 7);
}

TEST(d2_v11_battle_bg_short_is_no_image) {
  const std::string dir = tmpGameDir("bkg");
  if (dir.empty()) return;
  d2::BattleBg bg;
  std::vector<uint8_t> d(41007, 1);
  CHECK(core::writeFileAbs(dir + "/EBD_BKG/BKG000.EBD", d));
  CHECK(!d2::loadBattleBg(dir, "EBD_BKG/BKG000.EBD", bg));
  CHECK(!bg.valid);
  d.resize(41008, 1);
  CHECK(core::writeFileAbs(dir + "/EBD_BKG/BKG000.EBD", d));
  CHECK(d2::loadBattleBg(dir, "EBD_BKG/BKG000.EBD", bg));
  CHECK(bg.valid);
  CHECK(!d2::loadBattleBg(dir, "EBD_BKG/BKG001.EBD", bg));
}

TEST(d2_v11_roll_keys_buffer) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  auto g = synthGame(host, d2::Build::B9710);
  core::InputState up;
  up.rollUp = true;
  core::InputState down;
  down.rollDown = true;
  core::InputState none;
  host.script.clear();
  host.frame = 0;
  host.script.push_back(up);
  for (int i = 0; i < 40; ++i) host.script.push_back(up);  // held: repeat at 28, then every 3
  host.script.push_back(none);
  host.script.push_back(down);
  host.script.push_back(none);
  g->clearKeys();
  g->cur = core::InputState();
  for (size_t i = 0; i < host.script.size(); ++i) g->vsync();
  std::vector<int> keys;
  for (int k; (k = g->popKey()) != d2::kKeyNone;) keys.push_back(k);
  // 41 frames held: press + repeats at counts 28, 31, 34, 37, 40 -> 6 ROLL UP, then 1 ROLL DOWN
  int ups = 0, downs = 0;
  for (int k : keys) {
    if (k == d2::kKeyRollUp) ++ups;
    if (k == d2::kKeyRollDown) ++downs;
  }
  CHECK_EQ(ups, 6);
  CHECK_EQ(downs, 1);
  CHECK_EQ(keys.back(), d2::kKeyRollDown);
}

TEST(d2_v11_debug_start_every_new_game) {
  if (!have(kSynthRoot)) return;
  RecHost host;
  core::StartOptions opt;
  opt.gold = 500;
  opt.items = {3, 4};
  auto g = std::make_unique<d2::Game>(host, kSynthRoot, opt);
  for (int round = 0; round < 2; ++round) {
    std::memset(g->inventory, 0, sizeof(g->inventory));
    g->st.gold = 0;
    g->applyDebugStart(true);
    CHECK_EQ(g->st.gold, 500u);
    CHECK_EQ(g->itemCount(3), 1);
    CHECK_EQ(g->itemCount(4), 1);
  }
  g->st.gold = 0;
  g->applyDebugStart(false);  // Continue: not applied
  CHECK_EQ(g->st.gold, 0u);
}
