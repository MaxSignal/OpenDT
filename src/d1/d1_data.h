// D1 data files (spec 10).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/screen.h"

namespace d1 {

struct HeroDef {
  uint8_t name[17];
  int level = 1;
  int gStr = 0, gAgi = 0, gVit = 0, gInt = 0, gLuck = 0;
  int graphic = 0;
  uint8_t magicLv[32];
};

struct StartInfo {
  int screen = 0, pos = 0, gold = 0, bgm = 0;
  uint8_t party[4];
  int shipScreen = 0, shipPos = 0, shipGfx = 0;
  int airScreen = 0, airPos = 0, airGfx = 0;
  int battleBgm = 0, ring = 0, shipBgm = 0, airBgm = 0;
};

struct ItemDef {
  uint8_t name[17];
  int price = 0, kind = 0, value = 0, equipMask = 0;
};

struct MagicDef {
  uint8_t name[17];
  int mp = 0, power = 0;
};

struct EnemyDef {
  uint8_t name[17];
  int image = 0, hp = 0, atk = 0, def = 0, agi = 0, exp = 0, gold = 0;
  uint8_t magicMask[4];
  uint8_t act[5];
  uint8_t flags = 0;
};

struct Data {
  std::string root;
  HeroDef heroes[8];
  StartInfo start;
  ItemDef items[128];
  MagicDef magics[32];
  EnemyDef enemies[160];
  uint8_t enemyMap[1024];
  std::vector<core::Image> chips;  // 512 x 16x16
  uint16_t patChips[256][4];
  uint8_t landBits[32], blockBits[32];
  std::vector<uint8_t> maps[16];    // empty -> all zero
  std::vector<uint8_t> ivents[16];  // FF FF FF FF if missing
  bool chrOk[64];
  std::vector<core::Image> chr[64];  // 8 frames 32x32
  core::Image chrA[64], chrB[64];
  core::Image enemyImg[48];
  uint8_t palet[16][3], palet2[16][3];
  bool hasTitle = false;
  std::vector<uint8_t> titleIdx;  // 640x400 indices
  uint8_t titlePal[16][3];

  void load(const std::string& root);

  bool land(int p) const { return (landBits[p >> 3] & (0x80 >> (p & 7))) != 0; }
  bool blocked(int p) const { return (blockBits[p >> 3] & (0x80 >> (p & 7))) != 0; }
  uint8_t mapCell(int screen, int x, int y) const;
};

// Pattern drawn for object graphics 0x40..0x63 (spec 10 §9.4).
int objPattern(int gfx);

// Event file helpers (spec 10 §9).
struct PageHdr {
  int start;  // offset of the FF FF
  int item, party, sw, gold, gfx, move, trig;
};
int findFFFF(const std::vector<uint8_t>& b, int from);
void eventPages(const std::vector<uint8_t>& b, int e, PageHdr pages[4]);
int nextEvent(const std::vector<uint8_t>& b, int e);
bool eventEnd(const std::vector<uint8_t>& b, int e);
int eventScreen(const std::vector<uint8_t>& b, int e);
int cmdLen(const std::vector<uint8_t>& b, int pc);
int signedArg(const uint8_t* p, bool* neg);  // sign-magnitude u16LE

int expNeeded(int level);

}  // namespace d1
