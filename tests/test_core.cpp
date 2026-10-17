// Acceptance checks from spec 01.
#include <cstring>

#include "core/screen.h"
#include "core/text.h"
#include "core/timing.h"
#include "core/util.h"
#include "test.h"

TEST(core_rng_turbo_c) {
  core::Rng r;
  const int expect[] = {346, 130, 10982, 1090, 11656, 7117, 17595, 6415, 22948, 31126};
  for (int e : expect) CHECK_EQ(r.next(), e);
}

TEST(core_sjis_to_jis) {
  CHECK_EQ(core::sjisToJis(0x81, 0x40), 0x2121);
  CHECK_EQ(core::sjisToJis(0x82, 0x9F), 0x2421);
  CHECK_EQ(core::sjisToJis(0x9F, 0xFC), 0x5E7E);
  CHECK_EQ(core::sjisToJis(0xE0, 0x40), 0x5F21);
  CHECK_EQ(core::sjisToJis(0xEA, 0xA4), 0x7426);
  CHECK_EQ(core::sjisToJis(0x82, 0xA0), 0x2422);
  CHECK_EQ(core::sjisToJis(0xEB, 0xB3), 0x7635);
}

TEST(core_ms_balance_d1) {
  core::MsBalance b(core::kD1MsHz);
  int total = 0;
  for (int i = 0; i < 16; ++i) total += b.add(20);
  CHECK_EQ(total, 18);
}

TEST(core_utf8_to_sjis) {
  // 風薫るとき
  std::string s = core::utf8ToSjis("\xE9\xA2\xA8\xE8\x96\xAB\xE3\x82\x8B\xE3\x81\xA8\xE3\x81\x8D");
  CHECK_EQ(s.size(), size_t(10));
  CHECK_EQ(uint8_t(s[0]), 0x95);
  CHECK_EQ(uint8_t(s[1]), 0x97);
  // ﾃﾞ -> 1-byte kana
  std::string k = core::utf8ToSjis("\xEF\xBE\x83\xEF\xBE\x9E");
  CHECK_EQ(k.size(), size_t(2));
  CHECK_EQ(uint8_t(k[0]), 0xC3);
  // invalid UTF-8 returned as is
  std::string bad = "\x83\x65\x83\x58";
  CHECK(core::utf8ToSjis(bad) == bad);
}

TEST(core_scroll_compose) {
  core::Screen s;
  s.setPal(1, 15, 0, 0);
  s.setPal(2, 0, 15, 0);
  for (int x = 0; x < 640; ++x) s.pset(x, 10, 1);
  s.scrollLine = 10;
  std::vector<uint8_t> rgb(640 * 400 * 3);
  s.compose(rgb.data());
  CHECK_EQ(rgb[0], 255);
  s.scrollLine = 0;
  s.hOffset = 1;
  s.pset(0, 11, 2);
  s.compose(rgb.data());
  // row 10's right 16 dots show row 11's left 16 dots
  CHECK_EQ(rgb[(10 * 640 + 624) * 3 + 1], 255);
}

TEST(core_text_layer) {
  core::Screen s;
  s.textEnabled = true;
  s.maskTextAll();
  s.unmaskTextRect(2, 0, 77, 23);
  std::vector<uint8_t> rgb(640 * 400 * 3, 0);
  s.setPal(0, 15, 15, 15);
  s.compose(rgb.data());
  CHECK_EQ(rgb[(100 * 640 + 5) * 3], 0);      // left mask black
  CHECK_EQ(rgb[(100 * 640 + 100) * 3], 255);  // interior shows graphics
  CHECK_EQ(rgb[(390 * 640 + 100) * 3], 0);    // bottom mask
}

TEST(core_trim_name) {
  const uint8_t a[] = {'A', 'B', ' ', 0x81, 0x40, ' ', 0};
  CHECK(core::trimName(a, 7) == "AB");
}
