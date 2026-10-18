// Acceptance checks for the D2 event interpreter (spec 22 §9).
#include <cstring>
#include <functional>
#include <vector>

#include "d2/events.h"
#include "d2/game.h"
#include "test.h"

namespace {

const char* kRoot = "/home/claude/samples/games/dino/dino";

struct EvHost : core::Host {
  long frame = 0;
  long limit = 200000;
  std::function<core::InputState(long)> script;
  std::function<void(const core::Screen&)> onVsync;
  std::vector<core::SoundRequest> sounds;
  void vsync(const core::Screen& s) override {
    ++frame;
    if (onVsync) onVsync(s);
    if (frame > limit) throw core::QuitGame();
  }
  core::InputState input() override { return script ? script(frame) : core::InputState(); }
  void sound(const core::SoundRequest& r) override { sounds.push_back(r); }
};

struct Cmd {
  std::vector<uint8_t> b;
  Cmd& op(int c, int depth, std::vector<uint8_t> args = {}) {
    b.push_back(uint8_t(c));
    b.push_back(uint8_t(args.size()));
    b.push_back(uint8_t(depth));
    b.insert(b.end(), args.begin(), args.end());
    return *this;
  }
  std::vector<uint8_t> done() {
    b.push_back(0);
    return b;
  }
};

struct Fixture {
  EvHost host;
  core::StartOptions opt;
  d2::Game g{host, kRoot, opt};
  Fixture() {
    g.st.eventRunning = 1;
    g.run_.startMap = g.st.curMap;
  }
  int run(const std::vector<uint8_t>& s) { return d2::runScript(g, 0, s.data(), s.size()); }
};

// A game positioned on a map (as after "new game"), with an event running.
struct MapFixture {
  EvHost host;
  core::StartOptions opt;
  d2::Game g{host, kRoot, opt};
  int self = 0;
  explicit MapFixture(int mapNo = -1, int x = 8, int y = 8) {
    g.recalcAll();
    g.newGameInit();
    if (mapNo >= 0) {
      g.st.curMap = uint16_t(mapNo);
      g.st.x = uint8_t(x);
      g.st.y = uint8_t(y);
    }
    g.enterMap();
    g.fieldMask();
    g.drawField();
    g.setTone(100);
    g.st.eventRunning = 1;
    g.run_.startMap = g.st.curMap;
    host.frame = 0;
  }
  int run(const std::vector<uint8_t>& s) { return d2::runScript(g, self, s.data(), s.size()); }
};

bool anyPixel(const core::Screen& s, int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
      if (s.pix[y * 640 + x]) return true;
  return false;
}

}  // namespace

TEST(d2ev_move_wait_ticks) {
  CHECK_EQ(d2::moveWaitTicks(0), 0);
  CHECK_EQ(d2::moveWaitTicks(4), 4);
  CHECK_EQ(d2::moveWaitTicks(5), 2);
  CHECK_EQ(d2::moveWaitTicks(15), 12);
  CHECK_EQ(d2::moveWaitTicks(17), 8);
  CHECK_EQ(d2::moveWaitTicks(18), 235);
  CHECK_EQ(d2::moveWaitTicks(19), 183);
  CHECK_EQ(d2::moveWaitTicks(20), 0);
}

TEST(d2ev_skip_search) {
  Cmd c;
  c.op(0x66, 0).op(0x01, 1, {'a'}).op(0x63, 1).op(0x67, 1).op(0x67, 0).op(0x68, 0);
  auto s = c.done();
  std::vector<uint8_t> w = s;
  const size_t size = w.size() + 64;
  w.resize(size + 600, 0);
  // from offset 3 search 0x67/0x68 depth 0: skips the depth-1 0x67
  CHECK_EQ(d2::skipTo(w, size, 3, 0x67, 0x68, 0), size_t(3 + 4 + 3 + 3));
  // not found -> size-3
  CHECK_EQ(d2::skipTo(w, size, 0, 0x6E, 0x6E, 0), size - 3);
}

TEST(d2ev_variables_clamp) {
  Fixture f;
  Cmd c;
  c.op(0x04, 0, {2, 50}).op(0x05, 0, {2, 60}).op(0x04, 0, {103, 10}).op(0x06, 0, {3, 20}).op(0x07, 0, {4, 5});
  f.run(c.done());
  CHECK_EQ(f.g.vars[2], 99);
  CHECK_EQ(f.g.vars[3], 0);  // 103 mod 100 = 3: 10 - 20 -> 0
  CHECK(f.g.vars[4] >= 0 && f.g.vars[4] <= 5);
}

TEST(d2ev_labels) {
  Fixture f;
  // recorded label: loop until var0 >= 3
  Cmd c;
  c.op(0x0C, 0, {0}).op(0x05, 0, {0, 1}).op(0x5A, 0, {3, 0, 3, 0, 0}).op(0x0B, 1).op(0x63, 1).op(0x6F, 0).op(0x0D, 0, {5});
  f.run(c.done());
  CHECK_EQ(f.g.vars[0], 3);
  // label 0 recorded and jump to label 5 (== 0 mod 5) resumes right after the label
  Fixture f2;
  Cmd d;
  d.op(0x05, 0, {1, 1}).op(0x5A, 0, {3, 1, 2, 0, 0}).op(0x0B, 1).op(0x63, 1).op(0x6F, 0).op(0x0D, 0, {1});
  f2.run(d.done());
  CHECK_EQ(f2.g.vars[1], 2);  // unrecorded label -> restart from the top
}

TEST(d2ev_condition_false_skips_to_6f) {
  Fixture f;
  Cmd c;
  c.op(0x5A, 0, {0, 10, 0, 0, 0}).op(0x04, 1, {7, 1}).op(0x63, 1).op(0x6F, 0).op(0x04, 0, {8, 1});
  f.run(c.done());
  CHECK_EQ(f.g.vars[7], 0);
  CHECK_EQ(f.g.vars[8], 1);
  f.g.switches[10] = 1;
  f.run(c.done());
  CHECK_EQ(f.g.vars[7], 1);
}

static std::vector<uint8_t> yesNoScript() {
  Cmd c;
  c.op(0x08, 0)
      .op(0x66, 0)
      .op(0x04, 1, {0, 1})
      .op(0x63, 1)
      .op(0x67, 0)
      .op(0x04, 1, {0, 2})
      .op(0x63, 1)
      .op(0x68, 0)
      .op(0x04, 0, {1, 9});
  return c.done();
}

TEST(d2ev_yes_no_branch) {
  {
    Fixture f;
    f.host.script = [](long fr) {
      core::InputState s;
      s.ok = fr >= 10 && fr < 14;
      return s;
    };
    f.run(yesNoScript());
    CHECK_EQ(f.g.vars[0], 1);
    CHECK_EQ(f.g.vars[1], 9);
  }
  {
    Fixture f;
    f.host.script = [](long fr) {
      core::InputState s;
      s.cancel = fr >= 10 && fr < 14;  // cancel = no
      return s;
    };
    f.run(yesNoScript());
    CHECK_EQ(f.g.vars[0], 2);
    CHECK_EQ(f.g.vars[1], 9);
  }
}

TEST(d2ev_choice_cancel_item) {
  // choice with ".B" cancel item: cancel selects index 1
  Fixture f;
  f.host.script = [](long fr) {
    core::InputState s;
    s.cancel = fr >= 10 && fr < 14;
    return s;
  };
  Cmd c;
  c.op(0x09, 0)
      .op(0x69, 0, {'A'})
      .op(0x69, 0, {'.', 'B'})
      .op(0x6A, 0)
      .op(0x04, 1, {0, 1})
      .op(0x63, 1)
      .op(0x6B, 0)
      .op(0x04, 1, {0, 2})
      .op(0x63, 1)
      .op(0x6E, 0);
  f.run(c.done());
  CHECK_EQ(f.g.vars[0], 2);
}

TEST(d2ev_nested_results_by_depth) {
  // depth-0 yes/no result 0 and depth-1 yes/no result 1 must not interfere
  Fixture f;
  int presses = 0;
  f.host.script = [&](long fr) {
    core::InputState s;
    // first choice: OK around frame 10; second: cancel around frame 60
    s.ok = fr >= 10 && fr < 14;
    s.cancel = fr >= 60 && fr < 64;
    (void)presses;
    return s;
  };
  Cmd c;
  c.op(0x08, 0)
      .op(0x66, 0)
      .op(0x08, 1)
      .op(0x66, 1)
      .op(0x04, 2, {0, 1})
      .op(0x63, 2)
      .op(0x67, 1)
      .op(0x04, 2, {0, 2})
      .op(0x63, 2)
      .op(0x68, 1)
      .op(0x04, 1, {1, 5})
      .op(0x63, 1)
      .op(0x67, 0)
      .op(0x04, 1, {1, 6})
      .op(0x63, 1)
      .op(0x68, 0);
  f.run(c.done());
  CHECK_EQ(f.g.vars[0], 2);
  CHECK_EQ(f.g.vars[1], 5);
}

TEST(d2ev_hp_mp_rules) {
  Fixture f;
  d2::Member& m = f.g.members[0];
  m.hp = 10;
  m.maxHp = 100;
  m.mp = 10;
  m.maxMp = 50;
  m.alive = 1;
  Cmd c;
  c.op(0x23, 0, {0, 0x10, 0x27});  // -10000 HP -> 1
  c.op(0x25, 0, {0, 0x40, 0x9C});  // -40000 MP -> 25546 (16-bit wrap)
  f.run(c.done());
  CHECK_EQ(m.hp, 1);
  CHECK_EQ(m.mp, 25546);
  Cmd d;
  d.op(0x24, 0, {0, 5, 0});  // MP + 5 clamps to max
  f.run(d.done());
  CHECK_EQ(m.mp, 50);
}

TEST(d2ev_game_over_check) {
  Fixture f;
  f.g.st.partyCount = 1;
  f.g.st.party[0] = 0;
  f.g.members[0].alive = 1;
  f.g.members[0].hp = 5;
  f.host.script = [](long fr) {
    core::InputState s;
    s.ok = (fr / 8) % 2 == 1;
    return s;
  };
  Cmd c;
  c.op(0x28, 0, {0x0D}).op(0x04, 0, {0, 1});  // paralyze whole party, next command triggers game over
  bool title = false;
  try {
    f.run(c.done());
  } catch (d2::ReturnToTitle&) {
    title = true;
  }
  CHECK(title);
  CHECK_EQ(f.g.vars[0], 0);
}

TEST(d2ev_warp_points) {
  Fixture f;
  f.g.st.warpCount = 0;
  Cmd c;
  for (int k = 0; k < 32; ++k) c.op(0x2D, 0, {uint8_t(k), 0, uint8_t(k), 1});
  c.op(0x2D, 0, {3, 0, 77, 78});  // update existing map 3
  c.op(0x2E, 0, {0, 0});          // delete map 0
  f.run(c.done());
  CHECK_EQ(f.g.st.warpCount, 29);
  CHECK_EQ(f.g.st.warps[0].map, 1);
  CHECK_EQ(f.g.st.warps[2].map, 3);
  CHECK_EQ(f.g.st.warps[2].x, 77);
}

TEST(d2ev_conditions) {
  Fixture f;
  f.g.st.partyCount = 0;
  uint8_t any[5] = {20, 0x0C, 0, 0, 0}, all[5] = {20, 0x0D, 0, 0, 0};
  CHECK(!d2::evalCondition(f.g, any));  // empty party: anyone false
  CHECK(d2::evalCondition(f.g, all));   // empty party: all true
  f.g.st.gold = 70000;
  uint8_t gold[5] = {10, 0x70, 0x11, 0x01, 0};  // 70000 = 0x11170
  CHECK(d2::evalCondition(f.g, gold));
  gold[1] = 0x71;
  CHECK(!d2::evalCondition(f.g, gold));
  uint8_t sw[5] = {1, 0xE9, 0x03, 0, 0};  // switch 1001 OFF -> false (out of range)
  CHECK(!d2::evalCondition(f.g, sw));
  uint8_t bad[5] = {24, 0, 0, 0, 0};
  CHECK(!d2::evalCondition(f.g, bad));
}

// ---- commands used by the sample games (spec 22 §8) ----

TEST(d2ev_switches) {
  Fixture f;
  Cmd c;
  c.op(0x02, 0, {5, 0}).op(0x02, 0, {0xE8, 0x03}).op(0x02, 0, {0xE9, 0x03}).op(0x02, 0, {6, 0}).op(0x03, 0, {6, 0});
  f.run(c.done());
  CHECK_EQ(f.g.switches[5], 1);
  CHECK_EQ(f.g.switches[1000], 1);  // 1000 is valid, 1001 is ignored
  CHECK_EQ(f.g.switches[6], 0);
}

TEST(d2ev_noop_commands) {
  // 0x74.., 0x40, standalone 0x65/0x69/0x70 only skip their argument length
  Fixture f;
  Cmd c;
  c.op(0x74, 0, {0x04, 0, 0}).op(0x40, 0, {1, 2}).op(0x65, 0, {'x'}).op(0x69, 0, {'y'}).op(0x70, 0, {3});
  c.op(0xFF, 0, {9}).op(0x04, 0, {0, 42});
  f.run(c.done());
  CHECK_EQ(f.g.vars[0], 42);
  CHECK_EQ(f.g.itemCount(3), 0);
}

TEST(d2ev_text_lines_and_window) {
  // 0x01 takes at most three 0x65 lines; "\^" in any line suppresses the final key wait
  MapFixture f;
  f.host.limit = 3000;
  Cmd c;
  c.op(0x01, 0, {'A'}).op(0x65, 0, {'B'}).op(0x65, 0, {'C'}).op(0x65, 0, {'D', '\\', '^'}).op(0x65, 0, {'E'});
  c.op(0x04, 0, {0, 1});
  f.run(c.done());
  CHECK_EQ(f.g.vars[0], 1);
  CHECK_EQ(f.g.st.msgOpen, 1);                             // window stays open
  CHECK_EQ(int(f.g.scr.pix[240 * 640 + 80]), 14);          // frame at (80,240)
  CHECK(anyPixel(f.g.scr, 96, 256, 103, 271));             // line 0
  CHECK(anyPixel(f.g.scr, 96, 256 + 72, 103, 271 + 72));   // line 3
  CHECK(!anyPixel(f.g.scr, 96, 352, 103, 362));            // a 4th 0x65 is not displayed
}

TEST(d2ev_text_controls_timing) {
  // "\." waits 400 ms (22.6 VSYNC), "\!" waits for release then press
  MapFixture f;
  f.host.limit = 3000;
  Cmd c;
  c.op(0x01, 0, {'\\', '.', '\\', '^'});
  f.run(c.done());
  CHECK(f.host.frame >= 23 && f.host.frame <= 24);  // open (1 VSYNC) + 400 ms
  MapFixture h;
  h.host.limit = 3000;
  h.host.script = [](long fr) {
    core::InputState s;
    s.ok = fr < 5 || (fr >= 40 && fr < 45);  // held at the start, released, pressed again
    return s;
  };
  Cmd d;
  d.op(0x01, 0, {'\\', '!', '\\', '^'});
  h.run(d.done());
  CHECK(h.host.frame >= 40 && h.host.frame < 45);
}

TEST(d2ev_items_and_gold) {
  Fixture f;
  f.g.st.gold = 999990;
  Cmd c;
  c.op(0x14, 0, {3}).op(0x14, 0, {3}).op(0x15, 0, {3}).op(0x14, 0, {0xFF});
  c.op(0x16, 0, {100, 0, 0, 0}).op(0x04, 0, {0, 0});
  f.run(c.done());
  CHECK_EQ(f.g.itemCount(3), 1);
  CHECK_EQ(f.g.itemCount(0xFF), 0);
  CHECK_EQ(f.g.st.gold, 999999u);
  Cmd d;
  d.op(0x17, 0, {0xFF, 0xFF, 0xFF, 0x7F});
  f.run(d.done());
  CHECK_EQ(f.g.st.gold, 0u);
}

TEST(d2ev_equip_commands) {
  Fixture f;
  int weapon = -1;
  for (int i = 0; i < 255 && weapon < 0; ++i)
    if (f.g.db.items[i].kind == 1) weapon = i;
  CHECK(weapon >= 0);
  if (weapon < 0) return;
  d2::Member& m = f.g.members[1];
  m.equip[0] = 0xFF;
  Cmd c;
  c.op(0x18, 0, {1, uint8_t(weapon)});
  f.run(c.done());
  CHECK_EQ(m.equip[0], weapon);
  CHECK_EQ(f.g.itemCount(weapon), 0);  // not taken from the inventory
  Cmd d;
  d.op(0x19, 0, {1, 0}).op(0x19, 0, {12, 0}).op(0x19, 0, {1, 5});
  f.run(d.done());
  CHECK_EQ(m.equip[0], 0xFF);
  CHECK_EQ(f.g.itemCount(weapon), 1);  // returned to the inventory
}

TEST(d2ev_exp_and_level) {
  Fixture f;
  d2::Member& m = f.g.members[0];
  Cmd c;
  c.op(0x1E, 0, {0, 0xFF, 0xFF, 0xFF, 0x7F});  // clamps at 9999999
  f.run(c.done());
  CHECK_EQ(m.exp, 9999999u);
  Cmd d;
  d.op(0x1F, 0, {0, 0xFF, 0xFF, 0xFF, 0x7F});  // clamps at 0
  f.run(d.done());
  CHECK_EQ(m.exp, 0u);
  CHECK_EQ(int(m.level), 1);
  Cmd e;
  e.op(0x20, 0, {0, 2});
  f.run(e.done());
  CHECK_EQ(int(m.level), std::min(3, int(m.maxLevel)));
  const uint32_t base = m.exp;
  CHECK_EQ(int64_t(base), d2::Game::expForLevel(m, m.level));
  m.exp += 5;
  Cmd z;
  z.op(0x20, 0, {0, 0});  // n = 0 lowers the experience to the level's requirement
  f.run(z.done());
  CHECK_EQ(m.exp, base);
  Cmd dn;
  dn.op(0x21, 0, {0, 50});  // never below level 1
  f.run(dn.done());
  CHECK_EQ(int(m.level), 1);
  CHECK_EQ(m.exp, 0u);
}

TEST(d2ev_hp_recover_build_difference) {
  for (int b = 0; b < 2; ++b) {
    Fixture f;
    f.g.build = b == 0 ? d2::Build::B9710 : d2::Build::B9703;
    f.g.st.partyCount = 0;  // no game over check interference
    d2::Member& m = f.g.members[0];
    m.hp = 0;
    m.maxHp = 100;
    m.alive = 0;
    Cmd c;
    c.op(0x22, 0, {0, 10, 0});
    f.run(c.done());
    CHECK_EQ(int(m.hp), b == 0 ? 0 : 10);  // standard build: no change for HP 0
    CHECK_EQ(int(m.alive), 0);
  }
}

TEST(d2ev_status_commands) {
  Fixture f;
  f.g.st.partyCount = 2;
  f.g.st.party[0] = 0;
  f.g.st.party[1] = 1;
  for (int k = 0; k < 2; ++k) {
    f.g.members[k].hp = 5;
    f.g.members[k].alive = 1;
    std::memset(f.g.members[k].status, 0, 6);
  }
  Cmd c;
  c.op(0x26, 0, {1}).op(0x28, 0, {0x0C});  // 0x0C is not a valid target
  f.run(c.done());
  CHECK_EQ(f.g.members[1].status[0], 1);
  CHECK_EQ(f.g.st.poison, 1);
  CHECK_EQ(f.g.members[0].status[5], 0);
  Cmd d;
  d.op(0x2A, 0, {1}).op(0x2B, 0, {1});
  f.run(d.done());
  CHECK_EQ(int(f.g.members[1].hp), 1);
  CHECK_EQ(f.g.members[1].alive, 1);
  CHECK_EQ(f.g.members[1].status[0], 0);  // cleared by 0x2A
  CHECK_EQ(f.g.st.poison, 0);
  f.g.members[1].alive = 0;  // HP > 0 but not alive: 0x2B does nothing
  Cmd e;
  e.op(0x2B, 0, {1});
  f.run(e.done());
  CHECK_EQ(f.g.members[1].alive, 0);
}

TEST(d2ev_flags_and_escape_point) {
  Fixture f;
  std::memcpy(f.g.st.curBgm, "FIELD\0\0\0", 9);
  f.g.st.escBgm[8] = 0x5A;  // 0x30 sets only the 8 name characters
  Cmd c;
  c.op(0x2F, 0, {0}).op(0x31, 0, {0}).op(0x3B, 0, {0}).op(0x30, 0, {7, 0, 11, 12});
  f.run(c.done());
  CHECK_EQ(f.g.st.noWarp, 1);
  CHECK_EQ(f.g.st.noEscape, 1);
  CHECK_EQ(f.g.st.noReorder, 1);
  CHECK_EQ(f.g.st.escMap, 7);
  CHECK_EQ(f.g.st.escX, 11);
  CHECK_EQ(f.g.st.escY, 12);
  CHECK(std::memcmp(f.g.st.escBgm, "FIELD", 6) == 0);
  CHECK_EQ(int(uint8_t(f.g.st.escBgm[8])), 0x5A);
  Cmd d;
  d.op(0x2F, 0, {3}).op(0x31, 0, {1}).op(0x3B, 0, {9});
  f.run(d.done());
  CHECK_EQ(f.g.st.noWarp, 0);
  CHECK_EQ(f.g.st.noEscape, 0);
  CHECK_EQ(f.g.st.noReorder, 0);
}

TEST(d2ev_bgm_commands) {
  Fixture f;
  std::memcpy(f.g.st.curBgm, "FIELD\0\0\0", 9);
  std::memcpy(f.g.st.prevBgm, "OLD\0\0\0\0\0", 9);
  f.g.st.vehicle = 0;
  Cmd c;
  c.op(0x46, 0, {'E', 'V', 0, 0, 0, 0, 0, 0, 0}).op(0x46, 0, {'E', 'V', 0, 0, 0, 0, 0, 0, 0}).op(0x48, 0, {7});
  f.run(c.done());
  CHECK(std::strcmp(f.g.st.curBgm, "EV") == 0);
  CHECK(std::strcmp(f.g.st.prevBgm, "FIELD") == 0);
  CHECK(std::strcmp(f.g.map.hdr.battleBgm, "EV") == 0);
  int plays = 0, sfx = -1;
  for (auto& r : f.host.sounds) {
    if (r.kind == core::SoundRequest::PlayFile) ++plays;
    if (r.kind == core::SoundRequest::Sfx) sfx = r.value;
  }
  CHECK_EQ(plays, 1);  // the same name is not restarted
  CHECK_EQ(sfx, 17);   // SFX n -> driver n+10
  Cmd d;
  d.op(0x47, 0);
  f.run(d.done());
  CHECK(std::strcmp(f.g.st.curBgm, "FIELD") == 0);
  CHECK(std::strcmp(f.g.map.hdr.battleBgm, "FIELD") == 0);
  // on a vehicle the previous BGM is kept
  f.g.st.vehicle = 1;
  std::memcpy(f.g.st.prevBgm, "LAND\0\0\0\0", 9);
  f.run(c.done());
  CHECK(std::strcmp(f.g.st.prevBgm, "LAND") == 0);
  CHECK(std::strcmp(f.g.st.curBgm, "EV") == 0);
}

TEST(d2ev_fade_timing) {
  Fixture f;
  f.g.frameCounter = 77;
  Cmd c;
  c.op(0x34, 0, {1});
  f.run(c.done());
  CHECK_EQ(f.host.frame, 18L);  // 1 + 17 steps of 1 VSYNC
  CHECK_EQ(f.g.tone, 0);
  CHECK_EQ(f.g.frameCounter, 0u);
  f.host.frame = 0;
  Cmd d;
  d.op(0x35, 0, {2});
  f.run(d.done());
  CHECK_EQ(f.host.frame, 35L);  // 1 + 17 * 2
  CHECK_EQ(f.g.tone, 100);
  f.host.frame = 0;
  Cmd e;
  e.op(0x34, 0, {0}).op(0x38, 0, {0});  // immediate
  f.run(e.done());
  CHECK_EQ(f.host.frame, 0L);
  CHECK_EQ(f.g.tone, 200);
  Cmd w;
  w.op(0x39, 0, {1});
  f.run(w.done());
  CHECK_EQ(f.g.tone, 100);
}

TEST(d2ev_flash_and_shake) {
  Fixture f;
  std::vector<int> tones, offs;
  f.host.onVsync = [&](const core::Screen& s) {
    tones.push_back(f.g.tone);
    offs.push_back(s.hOffset);
  };
  f.g.setTone(100);
  Cmd c;
  c.op(0x32, 0);
  f.run(c.done());
  const std::vector<int> want = {100, 120, 126, 132, 138, 144, 150, 156, 162, 156, 144, 132, 120};
  CHECK(tones == want);
  CHECK_EQ(f.g.tone, 100);
  offs.clear();
  Cmd d;
  d.op(0x33, 0);
  f.run(d.done());
  const std::vector<int> wantOff = {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1};
  CHECK(offs == wantOff);
  CHECK_EQ(f.g.scr.hOffset, 0);
}

TEST(d2ev_wait_command) {
  Fixture f;
  Cmd c;
  c.op(0x13, 0, {10}).op(0x13, 0, {0});
  f.run(c.done());
  CHECK(f.host.frame >= 56 && f.host.frame <= 57);  // 1000 ms at 56.42 Hz
}

TEST(d2ev_tone_change) {
  MapFixture f;
  Cmd c;
  c.op(0x36, 0, {4, 0});
  f.run(c.done());
  CHECK_EQ(f.g.st.tone, 5);
  bool ok = true;
  for (int i = 0; i < 16; ++i) {
    int R = f.g.basePal[i][0], G = f.g.basePal[i][1], B = f.g.basePal[i][2];
    R = std::max(0, R - 8);
    G = std::min(15, (R + G + B) / 3 + 3);
    B = std::max(0, B - 7);
    if (f.g.curPal[i][0] != R * 17 || f.g.curPal[i][1] != G * 17 || f.g.curPal[i][2] != B * 17) ok = false;
  }
  CHECK(ok);
  f.host.frame = 0;
  Cmd d;
  d.op(0x37, 0, {2});
  f.run(d.done());
  CHECK_EQ(f.host.frame, 15L);  // 1 + 7 * s
  CHECK_EQ(f.g.st.tone, 0);
  bool base = true;
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k)
      if (f.g.curPal[i][k] != f.g.basePal[i][k] * 17) base = false;
  CHECK(base);
}

TEST(d2ev_erase_place_swap) {
  MapFixture f(3, 9, 9);  // dino MAP003: events 0 (3,2), 1 (2,3), 7 (9,5), 8 (8,6)
  d2::EventRec* e0 = f.g.ev(0);
  d2::EventRec* e1 = f.g.ev(1);
  d2::EventRec* e7 = f.g.ev(7);
  d2::EventRec* e8 = f.g.ev(8);
  CHECK(e0 && e1 && e7 && e8);
  if (!(e0 && e1 && e7 && e8)) return;
  f.self = 0;
  Cmd c;
  c.op(0x0A, 0, {0xFF}).op(0x04, 0, {0, 1});  // erasing oneself does not stop the script
  f.run(c.done());
  CHECK_EQ(e0->page1, 9);
  CHECK_EQ(e0->active, 0);
  CHECK_EQ(f.g.vars[0], 1);
  CHECK(!f.g.run_.movedSelf);
  const int lx = e1->x, ly = e1->y;
  Cmd d;
  d.op(0x50, 0, {1, 0, 5, 5});
  f.run(d.done());
  CHECK_EQ(int(e1->x), 5);
  CHECK_EQ(int(e1->y), 5);
  CHECK_EQ(int((e1->layer == 0 ? f.g.upperMap : f.g.lowerMap)[5][5]), 1);
  CHECK(int((e1->layer == 0 ? f.g.upperMap : f.g.lowerMap)[ly][lx]) != 1);
  CHECK(!f.g.run_.movedSelf);
  f.self = 7;
  const int x7 = e7->x, y7 = e7->y, x8 = e8->x, y8 = e8->y;
  Cmd s;
  s.op(0x51, 0, {0xFF, 8}).op(0x51, 0, {7, 8});  // 0xFF is not "self" for 0x51
  f.run(s.done());
  CHECK_EQ(int(e7->x), x8);
  CHECK_EQ(int(e7->y), y8);
  CHECK_EQ(int(e8->x), x7);
  CHECK_EQ(int(e8->y), y7);
  CHECK(f.g.run_.movedSelf);
}

TEST(d2ev_map_move_and_event_commands) {
  MapFixture f(3, 9, 9);
  std::memcpy(f.g.run_.savedBattleBgm, "SAVED\0\0\0", 9);
  Cmd c;
  c.op(0x10, 0, {3, 0, 8, 8});  // same map: the saved battle BGM is kept
  f.run(c.done());
  CHECK(std::strcmp(f.g.run_.savedBattleBgm, "SAVED") == 0);
  CHECK_EQ(int(f.g.st.x), 8);
  Cmd d;
  d.op(0x10, 0, {6, 0, 4, 4}).op(0x50, 0, {0, 0, 2, 2}).op(0x56, 0, {1, 0x90, 4});
  f.run(d.done());
  CHECK_EQ(int(f.g.st.curMap), 6);
  CHECK(std::strcmp(f.g.run_.savedBattleBgm, f.g.map.hdr.battleBgm) == 0);
  d2::EventRec* e0 = f.g.ev(0);
  d2::EventRec* e1 = f.g.ev(1);
  CHECK(e0 && e1);
  if (e0) CHECK(!(e0->x == 2 && e0->y == 2));  // ignored after the map changed
  if (e1) CHECK_EQ(int(e1->moveCmd), 0);
}

TEST(d2ev_hero_move_commands) {
  MapFixture f;
  f.g.st.dir = 8;
  Cmd c;
  c.op(0x4A, 0, {3});  // face down: 4 ticks at v = 0, 3, 6, 9
  f.run(c.done());
  CHECK_EQ(int(f.g.st.dir), 2);
  CHECK_EQ(int(f.g.st.moveCmd), 0);
  CHECK_EQ(f.host.frame, 9L);
  f.host.frame = 0;
  Cmd d;
  d.op(0x4A, 0, {20});  // >= 20: no ticks
  f.run(d.done());
  CHECK_EQ(f.host.frame, 0L);
  Cmd e;
  e.op(0x55, 0, {6}).op(0x58, 0);  // the command persists until cleared
  f.run(e.done());
  CHECK_EQ(int(f.g.st.dir), 4);
  CHECK_EQ(int(f.g.st.moveCmd), 6);
  Cmd g;
  g.op(0x55, 0, {1}).op(0x59, 0);  // display direction fix survives the clear
  f.run(g.done());
  CHECK_EQ(int(f.g.st.moveCmd), 0);
  CHECK_EQ(int(f.g.st.dirFix), 4);
  f.host.frame = 0;
  Cmd t;
  t.op(0x4F, 0).op(0x57, 0, {2});  // 2 ticks (3 VSYNC) + 8 ticks (21 VSYNC)
  f.run(t.done());
  CHECK_EQ(f.host.frame, 3L + 21L);
}

TEST(d2ev_key_input_wait_0e) {
  MapFixture f;
  f.host.limit = 3000;
  f.host.script = [](long fr) {
    core::InputState s;
    s.cancel = fr < 4 || (fr >= 30 && fr < 40);
    return s;
  };
  Cmd c;
  c.op(0x01, 0, {'A', '\\', '^'}).op(0x0E, 0);
  f.run(c.done());
  CHECK_EQ(f.g.st.msgOpen, 0);  // the message window is closed first
  CHECK(f.host.frame >= 30 && f.host.frame <= 33);
}

TEST(d2ev_stop_aborts_in_test_play) {
  MapFixture f;
  f.host.script = [](long fr) {
    core::InputState s;
    s.debug = s.cancel = fr >= 5;
    return s;
  };
  f.g.testPlay = true;
  Cmd c;
  c.op(0x58, 0).op(0x13, 0, {20}).op(0x58, 0).op(0x04, 0, {0, 1});
  bool aborted = false;
  try {
    f.run(c.done());
  } catch (d2::EventAbort&) {
    aborted = true;
  }
  CHECK(aborted);
  CHECK_EQ(f.g.vars[0], 0);
  // without test play the same keys do nothing
  MapFixture h;
  h.host.script = f.host.script;
  h.g.testPlay = false;
  h.run(c.done());
  CHECK_EQ(h.g.vars[0], 1);
}

TEST(d2ev_title_and_quit) {
  Fixture f;
  Cmd c;
  c.op(0x61, 0).op(0x04, 0, {0, 1});
  bool title = false;
  try {
    f.run(c.done());
  } catch (d2::ReturnToTitle&) {
    title = true;
  }
  CHECK(title);
  Cmd d;
  d.op(0x62, 0);
  bool quit = false;
  try {
    f.run(d.done());
  } catch (d2::QuitProgram&) {
    quit = true;
  }
  CHECK(quit);
  CHECK(!f.host.sounds.empty() && f.host.sounds.back().kind == core::SoundRequest::Stop);
  CHECK_EQ(f.g.vars[0], 0);
}

TEST(d2ev_game_over_after_0x2a) {
  MapFixture f;
  f.host.limit = 5000;
  f.host.script = [](long fr) {
    core::InputState s;
    s.ok = (fr / 8) % 2 == 1;
    return s;
  };
  Cmd c;
  c.op(0x2A, 0, {0x0D}).op(0x04, 0, {0, 1});
  bool title = false;
  try {
    f.run(c.done());
  } catch (d2::ReturnToTitle&) {
    title = true;
  }
  CHECK(title);
  CHECK_EQ(f.g.vars[0], 0);
  CHECK_EQ(f.g.st.msgOpen, 1);
  CHECK(anyPixel(f.g.scr, 96, 256, 140, 271));  // the defeat message on line 0
}

TEST(d2ev_choice_window_layout) {
  // items of 2 and 5 bytes: w = 5, x = 0x200 - 40, window (x,0x50) (0x230-x) x 72; no cancel item
  MapFixture f;
  f.host.limit = 3000;
  int border = -1, right = -1, bottom = -1;
  f.host.onVsync = [&](const core::Screen& s) {
    if (f.host.frame == 20) {
      border = s.pix[0x50 * 640 + 0x1D8];
      right = s.pix[0x50 * 640 + 0x22F];
      bottom = s.pix[(0x50 + 71) * 640 + 0x1D8];
    }
  };
  f.host.script = [](long fr) {
    core::InputState s;
    s.cancel = fr >= 10 && fr < 14;  // ignored (no cancel item)
    s.down = fr >= 30 && fr < 34;
    s.ok = fr >= 50 && fr < 54;
    return s;
  };
  Cmd c;
  c.op(0x09, 0).op(0x69, 0, {'A', 'B'}).op(0x69, 0, {'C', 'D', 'E', 'F', 'G'});
  c.op(0x6A, 0).op(0x04, 1, {0, 1}).op(0x63, 1).op(0x6B, 0).op(0x04, 1, {0, 2}).op(0x63, 1).op(0x6E, 0);
  f.run(c.done());
  CHECK_EQ(border, 14);
  CHECK_EQ(right, 14);
  CHECK_EQ(bottom, 14);
  CHECK_EQ(f.g.vars[0], 2);
  CHECK(f.host.frame >= 50);
}

TEST(d2ev_consecutive_pictures) {
  // spec 22 §8 0x3A / §9-14: no field is shown between consecutive pictures
  MapFixture f;
  f.host.limit = 5000;
  std::vector<int> tones;
  f.host.onVsync = [&](const core::Screen&) { tones.push_back(f.g.tone); };
  f.host.script = [](long fr) {
    core::InputState s;
    s.ok = (fr / 10) % 2 == 1;
    return s;
  };
  Cmd c;
  c.op(0x3A, 0, {0, 0}).op(0x3A, 0, {0, 0}).op(0x13, 0, {1});
  f.run(c.done());
  int shown = 0;
  for (size_t i = 1; i < tones.size(); ++i)
    if (tones[i] == 100 && tones[i - 1] != 100) ++shown;
  CHECK_EQ(shown, 3);  // picture, picture, field
  CHECK_EQ(f.g.tone, 100);
}

TEST(d2ev_more_conditions) {
  MapFixture f(3, 9, 9);
  d2::Game& g = f.g;
  g.st.pendingEvent = 7;
  d2::EventRec* e7 = g.ev(7);
  CHECK(e7 != nullptr);
  if (!e7) return;
  g.st.dir = 6;
  uint8_t dir[5] = {5, 2, 0, 0, 0};
  CHECK(d2::evalCondition(g, dir));
  dir[1] = 1;
  CHECK(!d2::evalCondition(g, dir));
  e7->dir = 8;
  uint8_t edir[5] = {6, 0xFF, 3, 0, 0};  // self (the pending event) faces up
  CHECK(d2::evalCondition(g, edir));
  uint8_t pos[5] = {7, 4, 9, 0, 0};  // hero Y >= 9
  CHECK(d2::evalCondition(g, pos));
  pos[1] = 6;  // unknown comparison: false
  CHECK(!d2::evalCondition(g, pos));
  uint8_t epos[5] = {8, 0xFF, 0, uint8_t(e7->x), 0};
  CHECK(d2::evalCondition(g, epos));
  uint8_t epos2[5] = {8, 200, 0, 0, 0};  // nonexistent event
  CHECK(!d2::evalCondition(g, epos2));
  uint8_t item[5] = {9, 0xFF, 0, 0, 0};
  CHECK(!d2::evalCondition(g, item));
  g.st.partyCount = 1;
  g.st.party[0] = 2;
  uint8_t cnt[5] = {13, 1, 0, 0, 0}, in[5] = {14, 2, 0, 0, 0};
  CHECK(d2::evalCondition(g, cnt));
  CHECK(d2::evalCondition(g, in));
  in[1] = 3;
  CHECK(!d2::evalCondition(g, in));
  g.members[2].level = 7;
  g.members[2].hp = 30;
  g.members[2].maxHp = 30;
  g.members[2].alive = 0;
  uint8_t lv[5] = {15, 0x0C, 7, 0, 0}, full[5] = {17, 0x0D, 0, 0, 0}, dead[5] = {22, 2, 0, 0, 0};
  CHECK(d2::evalCondition(g, lv));
  lv[2] = 8;
  CHECK(!d2::evalCondition(g, lv));
  CHECK(d2::evalCondition(g, full));
  CHECK(d2::evalCondition(g, dead));
  uint8_t bad[5] = {17, 14, 0, 0, 0};  // member spec 14: false
  CHECK(!d2::evalCondition(g, bad));
  g.st.shopDone = 1;
  uint8_t shop[5] = {23, 0, 0, 0, 0};
  CHECK(d2::evalCondition(g, shop));
}
