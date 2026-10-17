// D1 engine state and logic (spec 11, 12).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/host.h"
#include "core/screen.h"
#include "core/text.h"
#include "core/timing.h"
#include "core/util.h"
#include "d1/d1_data.h"

namespace d1 {

// UTF-8 literal -> Shift-JIS bytes (strings in source are written in UTF-8).
std::string S(const char* u8);

constexpr uint8_t kNone = 0x80;   // empty item / equipment
constexpr int kErased = 0xFF;     // object x when erased

struct Hero {
  int level = 1;
  int status = 0;
  int hp = 0, mp = 0;
  int32_t exp = 0;
  uint8_t equip[5] = {kNone, kNone, kNone, kNone, kNone};
  int recMaxHp = 0, recMaxMp = 0;
};

struct Obj {
  int event = -1;  // event start offset in block buffer; -1 ship, -2 airship, -3 hero
  int gfx = 0xFF;
  int x = kErased, y = 0;
  int dir = 2;
  int stage = 0;
  int move = 0;
  int trig = 0;
};

enum class MoveResult { Moving, Edge, BlockedStopped, BlockedMoving, Terrain, Success };

struct MenuRes {
  enum Kind { Selected, Cancel, Next, Prev } kind;
  int pos;
};

// Battle combatant state (spec 12 §1).
struct BEnemy {
  uint8_t name[17];
  int status = 0, image = 0, hp = 0, maxHp = 0, exp = 0, gold = 0, atk = 0, def = 0, agi = 0;
  uint8_t magicMask[4];
  uint8_t act[5];
  uint8_t flags = 0;
  int col = 0;
};

class Game : public core::Engine {
 public:
  Game(core::Host& host, const std::string& root, const core::StartOptions& opt);
  void run() override;

  // ---- exposed for tests ----
  Data db;
  Hero heroes[8];
  int curScreen = 0, curBlock = 0, heroX = 0, heroY = 0;
  long gold = 0;
  int fieldBgm = 0;
  uint8_t party[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t switches[25] = {};
  uint8_t invId[50], invCnt[50];
  int shipScreen = 0, shipPos = 0, shipGfx = 0, shipBgm = 0;
  int airScreen = 0, airPos = 0, airGfx = 0, airBgm = 0;
  int warpScreen = 0, warpPos = 0, warpPal = 6, warpBgm = 0;
  int vehicle = 0, palMode = 6, battleBgm = 0, ring = 0;
  core::Rng rng;

  void newGame();
  bool loadSlot(int slot);
  std::vector<uint8_t> buildSave() const;
  bool applySave(const std::vector<uint8_t>& d);
  bool saveSlot(int slot);

  // ability formulas (spec 11 §10.1)
  int calcMaxHp(int c) const { return heroes[c].level * db.heroes[c].gVit + 30; }
  int calcMaxMp(int c) const { return heroes[c].level * db.heroes[c].gInt; }
  int calcStr(int c) const { return heroes[c].level * db.heroes[c].gStr + 10; }
  int calcVit(int c) const { return heroes[c].level * db.heroes[c].gVit + 10; }
  int calcInt(int c) const { return heroes[c].level * db.heroes[c].gInt; }
  int calcLuck(int c) const { return heroes[c].level * db.heroes[c].gLuck; }
  int calcAgi(int c) const;
  int calcAtk(int c) const;
  int calcDef(int c) const;
  int variance(int v);
  int addExp(int slot, int v);  // returns new level if leveled, else 0

  bool addItem(int a);
  void removeItem(int a, bool all);
  bool hasItem(int a) const;

  bool getSwitch(int s) const { return s >= 0 && s < 200 && (switches[s >> 3] & (0x80 >> (s & 7))); }
  void setSwitch(int s, bool on) {
    if (s < 0 || s >= 200) return;
    if (on)
      switches[s >> 3] |= uint8_t(0x80 >> (s & 7));
    else
      switches[s >> 3] &= uint8_t(~(0x80 >> (s & 7)));
  }
  bool validSlot(int s) const { return s >= 0 && s < 4 && party[s] < 8; }
  bool isGameOver() const;

  // battle (exposed for tests)
  int battle(int group, bool eventBattle);

 private:
  friend struct BattleCtx;
  core::Host& host_;
  core::StartOptions opt_;
  core::Screen scr_;
  core::MsBalance ms_{core::kD1MsHz};
  uint32_t frameCounter_ = 0;
  int anim_ = 0;
  uint64_t totalFrames_ = 0;
  bool ended_ = false;
  bool debugApplied_ = false;

  // field runtime
  std::vector<uint8_t> blockBuf_;
  uint8_t cells_[240];
  Obj objs_[241];
  int objCount_ = 1;
  int heroDir_ = 2;
  int heroChr_ = 0;
  int encCounter_ = 0;
  int invCursor_ = 0;
  int lastTouched_ = -1;
  uint32_t lastJudge_ = 0;
  int titleSlot_ = 0;

  // event runtime
  int evObj_ = -1;
  bool evChrLoaded_ = false;
  int evChr_ = 0;
  bool msgOpen_ = false;

  // message area
  int mX0_ = 20, mY0_ = 1, mW_ = 40, mH_ = 4, mC_ = -1, mR_ = 0;

  // ---- low level ----
  void frame();
  void frames(int n) {
    for (int i = 0; i < n; ++i) frame();
  }
  void waitMs(double ms);
  core::InputState in() { return host_.input(); }
  int dirKey(const core::InputState& s) const;
  bool anyKey(const core::InputState& s) const;
  void waitRelease();
  void waitPress();
  void resetAnim() {
    frameCounter_ = 1;
    anim_ = 1;
  }
  void se(int n);
  void bgmChange(int n);
  void bgmStop();
  void applyPal(int mode, int offset);
  void applyPalRaw(const uint8_t p[16][3], int add);
  void fade(int a, int b);

  // ---- UI ----
  void window(int x1, int y1, int x2, int y2);
  int drawStr(int col, int row, const std::string& s);
  int drawStrFull(int col, int row, const std::string& s);
  void cursor(int col, int row, bool show);
  MenuRes menu(const std::vector<std::string>& items, int w, int C, int R, int x, int y, int init, bool paging);
  int heroSelect(int init, int x, int y, bool showAbility);
  void prompt(int x1, int y1, int x2, int y2, const std::string& body, const std::string& sub = "");
  void goldWindow();
  std::string statusStr(int c) const;
  bool yesNo(int x, int y);
  void msgClear();
  void msgPut(const std::string& s);
  void msgNum(long v) { msgPut(std::to_string(v)); }
  void msgSetup(int y0, int h) {
    mY0_ = y0;
    mH_ = h;
  }
  std::string nameOf(const uint8_t* n17) const { return core::rawName(n17, 17); }
  std::string heroName(int c) const { return nameOf(db.heroes[c].name); }
  std::string itemName(int i) const { return nameOf(db.items[i & 0x7F].name); }

  // ---- field ----
  void titleScreen();
  bool titleMenu();
  void gameStart();
  void applyDebugStart();
  void fieldLoop();
  enum class FP { Continue, Restart, RestartFirst, Exit };
  FP frameProc();
  void drawCell(int x, int y, uint8_t* buf = nullptr);
  void redrawScreen();
  void reloadCells();
  void reloadBlock();
  void writeBackPositions(int leavingScreen);
  void setup(bool first);
  void drawObj(int i, bool advance);
  void drawAllObjs();
  bool npcUpdate();
  bool passable(int x, int y, int mode) const;
  bool landable(int x, int y) const;
  bool onWater(int x, int y) const;
  MoveResult tryMove(int i, int d, int mode, int* blocker);
  void randomMove(int i, int mode);
  int selectPage(const std::vector<uint8_t>& buf, int e, PageHdr* out) const;
  bool pageCond(const PageHdr& p) const;
  void touch(int s);
  void examine();
  bool switchScreen(int d);
  bool inRing(int X, int Y) const;
  void board(int tx, int ty);
  bool disembark(int tx, int ty);
  void vehicleCommon();
  void encounterCheck(bool* battled);
  void walkPoison();
  void randomEncounter(int group);
  int heroGraphicForParty() const;
  int findStandingObj() const;

  // ---- events ----
  bool runEvent(int o);  // returns false if aborted (game ended)
  void msgOpen();
  void msgClose();
  void eventYesNo(bool* yes);
  void shop(const uint8_t* args);
  int sellSelect();
  void ending();

  // ---- menus ----
  enum class MM { Normal, Reload, Quit };
  MM mainMenu();
  void partyWindow();
  bool itemMenu();
  int itemSelect();
  bool magicMenu();
  int magicSelect(int c);
  void equipMenu();
  int equipHeroList(const bool* can, int init);
  void statusScreen(int c);
  void abilityBlock(int c, int x, int y);
  void sortMenu();
  enum class SysRes { Back, Quit, Loaded };
  SysRes systemMenu();
  void switchMonitor();
  bool castField(int caster, int magic, int itemSlot);  // returns warped
  bool useItemField(int itemSlot);
  void afterLoadRebuild();

  // battle helpers in d1_battle.cpp
  friend class Battle;
};

}  // namespace d1
