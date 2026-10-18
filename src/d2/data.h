// D2 data formats (spec 20). Records are kept as packed structs whose byte layout matches the
// files, so saving is byte-exact. Requires a little-endian host.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace d2 {

// ---------------------------------------------------------------------------------------------
// RPG.EXE builds (spec 20 §10)
enum class Build { B9606, B9608, B9703, B9710 };
Build detectBuild(const std::string& root);
const char* buildName(Build b);  // "1996-06" ...
inline bool isOldBuild(Build b) { return b == Build::B9606 || b == Build::B9608; }

#pragma pack(push, 1)
// ---------------------------------------------------------------------------------------------
// PLAYER.DAT party record, 200 bytes (spec 20 §2.1)
struct Member {
  uint8_t level;          // 0x00
  uint8_t alive;          // 0x01
  uint8_t status[6];      // 0x02 poison, blind, silence, confuse, sleep, paralysis
  uint32_t exp;           // 0x08
  uint32_t nextExp;       // 0x0C
  int16_t hp;             // 0x10
  int16_t maxHp;          // 0x12
  int16_t mp;             // 0x14
  int16_t maxMp;          // 0x16
  int16_t base[4];        // 0x18 attack, defense, spirit, speed
  int16_t stat[4];        // 0x20 with equipment
  uint8_t equip[5];       // 0x28 weapon, armor, shield, helm, accessory (0xFF none)
  uint8_t learned[30];    // 0x2D
  uint16_t chrFile;       // 0x4B
  uint8_t chrPat;         // 0x4D
  uint8_t unused4e[2];    // 0x4E
  char name[20];          // 0x50
  uint8_t maxLevel;       // 0x64
  uint8_t initLevel;      // 0x65
  uint8_t growthK;        // 0x66
  uint8_t unk67;          // 0x67
  uint8_t expA, expB, expC;  // 0x68..0x6A
  int16_t lv1[6];         // 0x6B maxHP, maxMP, atk, def, spi, spd
  int16_t growth[6];      // 0x77
  uint8_t learnLevel[30]; // 0x83
  uint8_t magicList[30];  // 0xA1
  uint8_t unusedBF[9];    // 0xBF
};
static_assert(sizeof(Member) == 200, "Member size");

struct WarpPoint {
  uint16_t map;
  uint8_t x, y;
  char bgm[12];
};
static_assert(sizeof(WarpPoint) == 16, "WarpPoint size");

// PLAYER.DAT state block, 0x2A8 bytes (spec 20 §2.2)
struct StateBlock {
  uint8_t heroSprite;      // 0x000 0 walk, 8 ship, 0x10 airship
  uint16_t curMap;         // 0x001
  uint16_t loadedMap;      // 0x003
  uint8_t x, y;            // 0x005
  uint8_t layer;           // 0x007
  uint8_t dir;             // 0x008
  uint8_t scroll;          // 0x009
  uint8_t dirProg[4];      // 0x00A down, left, right, up
  uint8_t moveCmd;         // 0x00E
  uint8_t dirFix;          // 0x00F
  uint8_t fast;            // 0x010
  uint8_t vehicle;         // 0x011 0 walk, 1 ship, 2 airship
  uint8_t eventRunning;    // 0x012
  uint8_t msgOpen;         // 0x013
  uint8_t poison;          // 0x014
  uint16_t steps;          // 0x015
  uint8_t hidden;          // 0x017
  uint16_t shipChr;        // 0x018
  uint8_t shipPat;         // 0x01A
  uint16_t shipMap;        // 0x01B
  uint8_t shipX, shipY;    // 0x01D
  uint16_t airChr;         // 0x01F
  uint8_t airPat;          // 0x021
  uint16_t airMap;         // 0x022
  uint8_t airX, airY;      // 0x024
  uint8_t tone;            // 0x026 tone kind + 1
  uint16_t defChr[4];      // 0x027
  uint8_t shipAnim;        // 0x02F
  uint8_t airAnim;         // 0x030
  char shipBgm[9];         // 0x031
  char airBgm[9];          // 0x03A
  char defBattleBgm[9];    // 0x043
  char curBgm[9];          // 0x04C
  char prevBgm[9];         // 0x055
  uint8_t partyCount;      // 0x05E
  uint8_t party[5];        // 0x05F
  uint32_t gold;           // 0x064
  uint8_t pendingKind;     // 0x068
  uint8_t pendingEvent;    // 0x069
  uint8_t shopDone;        // 0x06A
  uint8_t forced;          // 0x06B
  uint16_t battleBg;       // 0x06C
  uint8_t noEncounter;     // 0x06E
  uint8_t noEscape;        // 0x06F
  uint8_t escapeReq;       // 0x070
  uint16_t escMap;         // 0x071
  uint8_t escX, escY;      // 0x073
  char escBgm[9];          // 0x075
  uint8_t noWarp;          // 0x07E
  uint8_t warpReq;         // 0x07F
  uint8_t warpSel;         // 0x080
  uint8_t warpCount;       // 0x081
  WarpPoint warps[30];     // 0x082
  uint8_t unk262[32];      // 0x262
  uint8_t heroMoved;       // 0x282
  uint16_t startMap;       // 0x283
  uint8_t noReorder;       // 0x285
  uint8_t unk286[34];      // 0x286
};
static_assert(sizeof(StateBlock) == 0x2A8, "StateBlock size");

// ITEM.DAT record, 40 bytes (spec 20 §3). Kind-specific fields are accessed by offset helpers.
struct Item {
  char name[16];        // 0x00
  uint8_t nul;          // 0x10
  uint8_t kind;         // 0x11
  uint32_t price;       // 0x12
  uint8_t noDiscard;    // 0x16
  uint8_t f[17];        // 0x17..0x27 kind specific
  // equipment (kind 1..5)
  uint16_t equipMask() const { return uint16_t(f[0] | (f[1] << 8)); }
  uint8_t elements() const { return f[2]; }
  int16_t mod(int i) const { return int16_t(f[3 + 2 * i] | (f[4 + 2 * i] << 8)); }  // atk,def,spi,spd
  uint8_t hitRate() const { return f[0x22 - 0x17]; }
  uint8_t addEffect() const { return f[0x23 - 0x17]; }
  uint8_t addRate() const { return f[0x24 - 0x17]; }
  // medicine (6,7)
  uint8_t consumable() const { return f[0]; }
  int16_t healHp() const { return int16_t(f[2] | (f[3] << 8)); }
  int16_t healMp() const { return int16_t(f[4] | (f[5] << 8)); }
  uint8_t cureBits() const { return f[6]; }
  // magic effect / magic book (8,9)
  uint8_t magicNo() const { return f[1]; }
  // switch (10)
  uint16_t switchNo() const { return uint16_t(f[0x1E - 0x17] | (f[0x1F - 0x17] << 8)); }
};
static_assert(sizeof(Item) == 40, "Item size");

// MAGIC.DAT record, 80 bytes (spec 20 §4)
struct Magic {
  char name[17];        // 0x00
  uint8_t target;       // 0x11 0 ally1, 1 allies, 2 enemy1, 3 enemies, 4 warp, 5 escape, 6 switch
  uint16_t switchNo;    // 0x12
  uint8_t effect[3];    // 0x14 bit n -> byte n/8 mask 0x80>>(n%8)
  uint16_t power;       // 0x17
  uint8_t atkCoef;      // 0x19
  uint8_t spiCoef;      // 0x1A
  uint8_t variance;     // 0x1B
  uint8_t success;      // 0x1C
  uint8_t mpCost;       // 0x1D
  uint8_t flash;        // 0x1E
  uint8_t flashRgb[3];  // 0x1F
  uint8_t failKind;     // 0x22
  uint8_t sfx;          // 0x23
  uint8_t unused24[5];  // 0x24
  char message[39];     // 0x29
  bool hasEffect(int n) const { return n >= 0 && n < 24 && (effect[n / 8] & (0x80 >> (n % 8))) != 0; }
};
static_assert(sizeof(Magic) == 80, "Magic size");

// ENEMY.DAT enemy record, 0x50 bytes (spec 20 §5.1)
struct Enemy {
  char name[17];          // 0x00
  uint8_t image;          // 0x11
  uint32_t exp;           // 0x12
  uint32_t gold;          // 0x16
  uint32_t maxHp;         // 0x1A
  int16_t mp;             // 0x1E
  int16_t atk, def, spi, spd;  // 0x20..0x26
  uint8_t resist[5];      // 0x28
  uint8_t unused2d;       // 0x2D
  uint8_t escapeRate;     // 0x2E
  uint8_t dropRate;       // 0x2F
  uint8_t dropItem;       // 0x30
  uint32_t transformHp;   // 0x31
  uint8_t transformTo;    // 0x35
  uint8_t actions[16];    // 0x36 (kind,arg) x 8
  uint8_t size;           // 0x46
  uint8_t unused47[9];    // 0x47
  int resistance(int i) const { return (resist[i / 4] >> (6 - 2 * (i % 4))) & 3; }
};
static_assert(sizeof(Enemy) == 0x50, "Enemy size");

struct EnemyGroup {
  char name[17];
  uint8_t enemies[5];
};
static_assert(sizeof(EnemyGroup) == 0x16, "EnemyGroup size");

// MAPnnn.DAT header (spec 20 §7.3.1)
struct MapHeader {
  uint8_t size;
  uint8_t outerChip;
  uint16_t chipset;
  uint16_t chr[4];
  uint8_t encRate;
  char battleBgm[9];
  char name[16];
  uint8_t unused26;
  uint8_t loop;
};
static_assert(sizeof(MapHeader) == 0x28, "MapHeader size");

// Event page, 0x13 bytes (spec 20 §7.3.5)
struct EventPage {
  uint8_t cond;        // 0 none, 1 switch, 2 item, 3 member, 4 gold
  uint16_t condSwitch;
  uint8_t condItem;
  uint16_t condMember;
  uint32_t condGold;
  uint16_t gfx;
  uint8_t moveType;
  uint8_t dirMode;
  uint8_t anim;
  uint8_t priority;
  uint8_t trigger;
  uint16_t script;
};
static_assert(sizeof(EventPage) == 0x13, "EventPage size");

// Event record, 0x62 bytes; first 0x12 bytes are the saved runtime part.
struct EventRec {
  uint8_t x, y;          // 0x00
  uint8_t layer;         // 0x02
  uint8_t dir;           // 0x03
  uint8_t prog;          // 0x04
  uint8_t timer;         // 0x05
  uint8_t dirProg[4];    // 0x06 down, left, right, up
  uint8_t moveCmd;       // 0x0A
  uint8_t dirFix;        // 0x0B
  uint8_t fast;          // 0x0C
  uint8_t redrawKind;    // 0x0D
  uint8_t redrawReq;     // 0x0E
  uint8_t page1;         // 0x0F current page + 1 (0 undecided, 9 erased)
  uint8_t active;        // 0x10
  uint8_t unused11[3];   // 0x11
  uint16_t selfNo;       // 0x14
  EventPage pages[4];    // 0x16
};
static_assert(sizeof(EventRec) == 0x62, "EventRec size");
#pragma pack(pop)

constexpr int kEventAreaSize = 32000;
constexpr int kMaxEvents = 250;
constexpr int kMapDim = 160;

// Loaded map (spec 20 §7.3)
struct MapData {
  bool fromFile = false;
  MapHeader hdr{};
  uint8_t grid[kMapDim][kMapDim];   // [y][x]
  uint8_t zones[0x960];
  uint16_t version = 0;
  uint16_t eventAreaSize = 0;
  uint16_t eventPos[kMaxEvents];
  std::vector<uint8_t> eventArea = std::vector<uint8_t>(kEventAreaSize, 0);
  EventRec* events[kMaxEvents];     // pointers into eventArea or nullptr

  // In-memory size: the raw header value capped to 1..160 (spec 20 §7.3.1 note). File offsets use
  // the raw value (0x2C + N*N).
  int n() const { return hdr.size == 0 ? 1 : (hdr.size > kMapDim ? kMapDim : hdr.size); }
  // Script bytes for a page (pointer + length incl. terminator); nullptr if out of range.
  const uint8_t* script(int ev, int page, uint16_t* len) const;
};

// Pattern: 640 bytes (mask, plane0..3, each 32 rows x 4 bytes)
constexpr int kPatternBytes = 640;

struct PatternSet {
  std::vector<uint8_t> data;  // count * 640
  uint8_t pal[16][3]{};       // 4-bit
  bool hasPal = false;
  int count = 0;
  const uint8_t* pattern(int i) const { return data.data() + size_t(i) * kPatternBytes; }
};

// Full screen picture (TITLE.DAT / PCTnnn.EBD)
struct Picture {
  bool valid = false;
  uint8_t pal8[16][3]{};       // 8-bit values (use upper nibble)
  std::vector<uint8_t> px;     // 640*400 indices
};

// Enemy image (EGD)
struct EnemyImage {
  bool valid = false;
  uint8_t pal[16][3]{};  // 4-bit
  int sizeKind = 0;      // 0 S (80x96), 1 L (160x128)
  int w = 0, h = 0;
  std::vector<uint8_t> mask;   // h * w/8
  std::vector<uint8_t> planes; // 4 * h * w/8
};

// Battle background (EBD)
struct BattleBg {
  bool valid = false;
  uint8_t pal[16][3]{};  // 4-bit
  std::vector<uint8_t> px;  // 640*128
};

// Chip attributes (MCPnnn.DAT, processed per spec 20 §8.2)
struct ChipAttr {
  uint8_t attr[256];
  uint8_t enc[256];
  uint16_t bg[256];
};

// Save file (spec 20 §9)
constexpr int kSaveSize = 0x1FB0;
constexpr int kInventorySlots = 260;

// Static game database loaded at startup.
struct Database {
  std::vector<uint8_t> playerFile;         // raw PLAYER.DAT (padded)
  Member members0[12];                     // initial records
  StateBlock state0;                       // initial state block (with the 2 overrides)
  Item items[256];
  Magic magics[256];
  Enemy enemies[256];
  EnemyGroup groups[256];
  uint8_t word[0x792];
  char mapNames[1000][17];                 // NUL terminated copies
  uint8_t gaiji[256 * 32];
  Picture title;

  // WORD.DAT string from offset (NUL-terminated, bounded by the buffer end).
  std::string wordAt(int off) const;
};

void loadDatabase(const std::string& root, Database& db);  // throws core::DataError
void loadMap(const std::string& root, int no, MapData& m, const StateBlock& st, uint16_t prevVersion);
void loadPatterns(const std::string& root, const std::string& rel, int count, PatternSet& out);
void loadChipAttr(const std::string& root, int no, ChipAttr& out);
bool loadPicture(const std::string& root, const std::string& rel, Picture& out);
bool loadEnemyImage(const std::string& root, const std::string& rel, EnemyImage& out);
// Returns false ("no image": draw nothing, keep the palette) when missing or under 41008 bytes.
bool loadBattleBg(const std::string& root, const std::string& rel, BattleBg& out);
std::string numberedName(const char* prefix, int n, const char* ext);  // e.g. ("ECD_CHR/CHR",5,".ECD")

}  // namespace d2
