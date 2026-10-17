// D2 engine core: state, helpers and field (spec 21). See docs/d2_architecture.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/host.h"
#include "core/screen.h"
#include "core/text.h"
#include "core/timing.h"
#include "core/util.h"
#include "d2/data.h"
#include "d2/hooks.h"

namespace d2 {

// Control-flow exceptions.
struct ReturnToTitle {};  // back to the title loop (game over, event "to title")
struct QuitProgram {};    // end the program (system menu "quit", event "game end")
struct EventAbort {};     // STOP key during test play (thrown by Game::checkStop)

// PC-98 scan codes used in the key buffer (spec 01 §4.3).
enum : uint8_t {
  kKeyEsc = 0x00,
  kKeyReturn = 0x1C,
  kKeySpace = 0x34,
  kKeyRollUp = 0x36,
  kKeyRollDown = 0x37,
  kKeyUp = 0x3A,
  kKeyLeft = 0x3B,
  kKeyRight = 0x3C,
  kKeyDown = 0x3D,
  kKeyPad8 = 0x43,
  kKeyPad4 = 0x46,
  kKeyPad6 = 0x48,
  kKeyPad2 = 0x4B,
  kKeyNone = 0xFF,
};
inline bool isOkKey(uint8_t k) { return k == kKeyReturn || k == kKeySpace; }
inline bool isUpKey(uint8_t k) { return k == kKeyUp || k == kKeyPad8; }
inline bool isDownKey(uint8_t k) { return k == kKeyDown || k == kKeyPad2; }
inline bool isLeftKey(uint8_t k) { return k == kKeyLeft || k == kKeyPad4; }
inline bool isRightKey(uint8_t k) { return k == kKeyRight || k == kKeyPad6; }

constexpr int kShipValue = 1000;  // "event value" meaning the ship is there (spec 21 §5.6)

class Game : public core::Engine {
 public:
  Game(core::Host& host, const std::string& root, const core::StartOptions& opt);
  void run() override;

  // ===== environment =====
  core::Host& host;
  std::string root;
  core::StartOptions opt;
  Build build;
  bool testPlay;
  core::Screen scr;
  core::Rng rng;
  core::MsBalance msBal{core::kD2MsHz};
  Database db;
  core::GaijiTable gaiji;

  // ===== persistent game state (saved) =====
  Member members[12];
  StateBlock st;
  uint8_t inventory[kInventorySlots][2];  // (item, count)
  uint8_t switches[1001];
  int16_t vars[100];

  // ===== map & graphics =====
  MapData map;
  ChipAttr chips;
  PatternSet chipset;        // 160 patterns of MCPnnn.ECD
  PatternSet chrSlot[4];     // 64 patterns each
  int chrLoaded[4] = {-1, -1, -1, -1};
  int chipsetLoaded = -1;
  uint8_t cache[33][kPatternBytes];  // pattern cache 0xA0..0xC0 (spec 21 §4.4)
  uint8_t upperMap[kMapDim][kMapDim];  // layer 0x00 events ([y][x]), 0xFF/0xFD empty
  uint8_t lowerMap[kMapDim][kMapDim];  // layer 0x20 events
  int viewX = 0, viewY = 0;  // view origin (map coords of view cell (0,0))

  // ===== palette =====
  uint8_t curPal[16][3];   // 8-bit components
  uint8_t basePal[16][3];  // 4-bit (chipset palette)
  int tone = 100;

  // ===== timing / input =====
  uint32_t frameCounter = 0;  // free-running counter (spec 21 §1.1)
  uint32_t waitCounter = 0;   // wait counter, reset by wait loops
  core::InputState cur, prev;
  uint8_t keyBuf[16];
  int keyCount = 0;
  int repeatKey = -1, repeatCount = 0;
  bool latch[5] = {false, false, false, false, false};  // down, left, right, up, ok/cancel
  bool lastAxisVertical = false;

  // ===== runtime field state =====
  int heroRot = 0, eventRot = 0;      // rotation counters (spec 21 §8.6)
  bool npcDone[kMaxEvents];           // processed marks per 3-VSYNC cycle
  bool shadowOn = false;              // airship shadow blinking
  int shadowCounter = 0;
  uint32_t lastCycle = 0;             // main loop recorded counter
  std::string embed;                  // embedded string for "\@" (events / shops)
  bool debugApplied = false;          // --gold/--item/--warp applied
  bool debugBattleDone = false;       // --battle applied

  // ===== frame / time helpers (spec 21 §1) =====
  void vsync();                        // one VSYNC: counters, shadow, present, input, key buffer
  void waitFrames(int n);
  void waitMs(double ms);              // 56.42 Hz conversion with carry
  void waitUnits(int n);               // n*7 (SHIFT: n*3) frames on the wait counter
  void sfx(int n);                     // D2 SFX number (driver gets n+10)
  void playBgm(const char* name9);     // DAT_BGM/<name>.MML from the start (silent if missing)
  void stopBgm();
  static bool bgmEqual(const char* a, const char* b);  // 9-byte compare
  static bool bgmEqual8(const char* a, const char* b); // 8-char compare

  // ===== input (spec 21 §1.3, 01 §4.3) =====
  int popKey();                        // kKeyNone if empty
  void clearKeys();
  void recordLatch();
  int readDirection();                 // 0,2,4,6,8 or 5
  bool anyHeld() const;
  void keyWait();                      // "release then press" wait, draws a random number per poll
  void checkStop();                    // throws EventAbort when STOP (Ctrl+Cancel) in test play during events
  bool stopPressed() const { return testPlay && cur.debug && cur.cancel; }

  // ===== palette (spec 21 §2) =====
  void applyPalette();                 // "表示更新"
  void setTone(int t) { tone = t; applyPalette(); }
  void vsyncTone(int t) { vsync(); setTone(t); }  // "1 VSYNC 待ち、T=t で表示更新"
  void setPal4(int i, int r, int g, int b);       // 4-bit values (x17)
  void blackOut(int s);
  void blackIn(int s);
  void whiteOut(int s);
  void whiteIn(int s);
  void toneChange(int kind, int s);    // spec 21 §2.4 / 22 §6.8 (s=-1: set only)
  void reapplyTone() { toneChange(int(st.tone) - 1, -1); }
  void flash();
  void shake();

  // ===== drawing helpers (spec 01 §2.6, 23 §2.1) =====
  void drawText(int x, int y, const std::string& sjis, int color = 15);  // bold, transparent
  int textWidth(const std::string& sjis) const;
  void drawWindow(int x, int y, int w, int h);
  void fieldMask();                    // text layer: field mask
  void menuMask();                     // text layer: menu mask (bottom row black)
  void windowMask();                   // text layer: left/right 8 dots black
  void clearTextLayer();
  int select(int x, int y, int n, int init, int cancelMode);  // spec 22 §6.4 (returns -1 on cancel)
  void drawCursor(int x, int y, int color);  // gaiji 0xEB9F
  // message window (spec 22 §6.1)
  void openMessage();
  void closeMessage();
  // One line of event text with control codes (spec 22 §6.2). Returns true if "\^" was seen.
  bool messageLine(int line, const std::string& s);
  void keyWaitMark();                  // wait mark at (312,352) + key wait (spec 22 §6.3)
  void goldWindow(int x, int y);       // spec 23 §2.5

  // ===== game rules (spec 20 §2.3, 23 §1) =====
  static int64_t expForLevel(const Member& m, int level);
  static int16_t statForLevel(const Member& m, int j, int level);
  void recalcLevel(Member& m);         // 23 §1.4
  void recalc(Member& m);              // 23 §1.5
  void recalcAll();
  bool levelUp(Member& m);             // 23 §1.6
  void levelDown(Member& m);           // 23 §1.7
  void addHp(Member& m, int n);        // 23 §1.8
  void fullHeal(Member& m);            // 23 §1.9
  bool gameOver() const;               // 23 §1.11
  bool inParty(int c) const;
  void joinParty(int c);
  void leaveParty(int c);
  int leader() const;                  // member number of party head (0 if >11)
  void learnMagic(Member& m, int magic);
  void forgetMagic(Member& m, int magic);
  bool addItem(int a);
  void packInventory();
  bool removeItem(int a);
  void moveItemToEnd(int a);
  void discardItem(int a);
  bool hasItem(int a) const;           // build difference G
  int itemCount(int a) const;
  void addGold(long n);
  void subGold(long n);
  void updatePoison();

  // ===== map / field (spec 21 §4-§12) =====
  void loadCurrentMap();               // spec 21 §4.1 (steps 1-4)
  void loadCurrentMapData();           // spec 21 §4.1 steps 1-3 (no tone reapply)
  void loadMapGraphics();              // spec 21 §4.1 step 2 (CHR slots, chipset, autotile cache)
  void enterMap();                     // spec 21 §4.2
  void invalidateGraphics() { for (auto& c : chrLoaded) c = -1; chipsetLoaded = -1; }
  void loadSpriteToCache(int base, int chrFile, int pat);
  void reloadHeroSprite();
  void reloadVehicleSprites();
  void reloadAllSprites();             // ship -> airship -> hero
  const uint8_t* pattern(int no) const;
  int attrAt(int x, int y) const;      // chip attribute at map coords (mod N)
  int chipAt(int x, int y) const;
  int wrap(int v) const { int n = map.n(); return n ? ((v % n) + n) % n : 0; }
  bool eventExists(int i) const { return i >= 0 && i < kMaxEvents && map.events[i] != nullptr; }
  EventRec* ev(int i) { return eventExists(i) ? map.events[i] : nullptr; }
  int activePage(int i) const;         // spec 21 §7.1 (-1 none)
  bool pageCondition(const EventPage& p) const;
  void updatePages();                  // spec 21 §7.2
  void eraseEvent(int i, bool permanent);  // spec 21 §7.5
  int eventAll(int x, int y) const;    // event at map position (both layers) or -1
  int sameLayerValue(int vx, int vy) const;   // view coords; may return kShipValue or 0xFF/0xFD
  int otherLayerValue(int vx, int vy) const;
  bool blockingEventAt(int mx, int my) const;  // spec 21 §6.6
  int eventPatternNo(int i, const EventPage& p) const;  // spec 21 §5.5
  void drawField(bool hideHero = false);  // spec 21 §5
  void drawFieldFull() { drawField(false); }
  void heroStep(int d, bool fast);     // spec 21 §6.1
  bool heroPassable(int d);            // spec 21 §6.5 (may trigger contact events)
  void updateHeroLayer();
  int heroProcess();                   // spec 21 §6.2
  void npcCycle();                     // spec 21 §8.1 (clears marks then all events)
  void npcProcess(int i);              // spec 21 §8.2
  bool npcTryStep(int i, int d, bool fast);
  void npcMoveTo(int i, int tx, int ty);
  void doMoveCommandHero();            // spec 21 §8.6
  void doMoveCommandEvent(int i);
  void forcedStep();                   // spec 21 §6.7
  bool canDisembarkWalk(int d);        // spec 21 §9.1
  bool canDisembarkShip(int d);
  void boardShip(bool withBgm);
  void leaveShip();
  void takeOff();
  void land();
  void airshipAnim(int startY, int dy);
  void shadowUpdate();
  int encounterCheck();                // spec 21 §10.1
  int callBattle(int group, int gameOverMode, int eraseEvent);  // spec 21 §10.2
  int tryStartEvent(int i, int t);     // spec 21 §7.3 (acceptance) -> hooks::runEvent
  void beginEvent(int i, int t);       // spec 21 §7.3 "起動する場合" 1-4 / 22 §4.1 (no labels)
  void endEvent();                     // spec 21 §7.4 / 22 §4.2
  void tick(int n);                    // spec 22 §6.5 "n tick 進める"
  void waitHeroStill();                // spec 22 §6.5 hero walk completion
  void waitEventStill(int i);          // spec 22 §6.5 event walk completion (build I)
  void shipBoardCheck();               // spec 22 §6.5 step 5
  void clearMoveCommands();            // spec 22 §6.6 "全移動命令の解除"
  void setHeroMoveCommand(int m);      // spec 22 §6.6
  void setEventMoveCommand(int i, int m);
  void fieldLoop();                    // spec 21 §11 (until exception)

  // event-run bookkeeping (shared with events.cpp)
  struct EventRun {
    int eventNo = -1;
    int trigger = 0;
    bool turned = false;
    uint8_t savedDir = 0;
    bool movedSelf = false;   // set by events.cpp when commands 0x4B/0x50/0x51/0x56 target self
    int startMap = -1;
    char savedBattleBgm[9] = {};
  } run_;

  // ===== flow (spec 21 §3, 20 §9) =====
  void titleLoop();
  void titleScreen();
  int titleMenu();                     // 0 new, 1 continued (loaded), 2 quit
  void resetToInitial();               // party/state from PLAYER.DAT
  void newGameInit();
  void applyDebugStart(bool newGame);
  int slotSelect(int x, int y);        // slot window (spec 23 §8.2); -1 cancel
  bool saveSlot(int k);
  bool loadSlot(int k);                // spec 20 §9.3 (blackout included); false if no file
  std::vector<uint8_t> buildSave() const;
};

}  // namespace d2
