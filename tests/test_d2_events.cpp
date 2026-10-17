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
  std::function<core::InputState(long)> script;
  void vsync(const core::Screen&) override {
    ++frame;
    if (frame > 200000) throw core::QuitGame();
  }
  core::InputState input() override { return script ? script(frame) : core::InputState(); }
  void sound(const core::SoundRequest&) override {}
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
