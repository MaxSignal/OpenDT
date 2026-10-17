// D2 game flow: startup, title, new game, save/load (spec 21 §3, 20 §9, 23 §8).
#include <cstring>

#include "d2/d2.h"
#include "d2/game.h"

namespace d2 {

namespace {
std::string sj(const char* utf8) { return core::utf8ToSjis(utf8); }
}  // namespace

void Game::run() {
  rng.seed(opt.seed);
  scr.textEnabled = true;
  try {
    titleLoop();
  } catch (QuitProgram&) {
  }
  stopBgm();
}

void Game::resetToInitial() {
  std::memcpy(members, db.members0, sizeof(members));
  st = db.state0;
  shadowOn = false;
  scr.hOffset = 0;
  scr.scrollLine = 0;
}

void Game::titleLoop() {
  for (;;) {
    stopBgm();
    resetToInitial();
    titleScreen();
    try {
      titleMenu();
      if (!debugBattleDone && opt.battle >= 0) {
        debugBattleDone = true;
        st.battleBg = chips.bg[chipAt(st.x, st.y)];
        callBattle(opt.battle & 0xFF, 1, -1);
      }
      fieldLoop();
    } catch (ReturnToTitle&) {
      continue;
    } catch (QuitProgram&) {
      if (opt.noQuit) continue;
      throw;
    }
  }
}

void Game::titleScreen() {
  vsync();
  setTone(0);
  clearTextLayer();
  scr.clear(0);
  scr.hOffset = 0;
  if (db.title.valid) {
    std::memcpy(curPal, db.title.pal8, sizeof(curPal));
    std::memcpy(scr.pix, db.title.px.data(), size_t(640) * 400);
    if (!testPlay) {
      blackIn(1);
      keyWait();
      blackOut(1);
    }
  } else {
    curPal[0][0] = 0;
    curPal[0][1] = 0x55;
    curPal[0][2] = 0x55;
    std::memset(curPal[14], 0, 3);
    std::memset(curPal[15], 0xFF, 3);
  }
}

int Game::titleMenu() {
  for (;;) {
    setTone(0);
    scr.clear(14);
    drawWindow(232, 144, 168, 96);
    drawText(264, 160, sj("ニューゲーム"));
    drawText(264, 184, sj("コンティニュー"));
    drawText(264, 208, sj("ゲーム終了"));
    vsync();
    setTone(100);
    const int sel = select(232, 144, 3, 0, 0);
    if (sel == 0) {
      blackOut(1);
      recalcAll();
      newGameInit();
      enterMap();
      playBgm(st.curBgm);
      applyDebugStart(true);
      return 0;
    }
    if (sel == 1) {
      const int k = slotSelect(264, 208);
      if (k < 0) continue;
      if (loadSlot(k)) return 1;
      continue;
    }
    blackOut(1);
    waitFrames(6);
    stopBgm();
    throw QuitProgram();
  }
}

void Game::newGameInit() {
  std::memset(inventory, 0, sizeof(inventory));
  std::memset(switches, 0, sizeof(switches));
  const int nv = build == Build::B9606 ? 50 : 100;
  for (int i = 0; i < nv; ++i) vars[i] = 0;
  st.heroSprite = 0;
  st.curMap = st.startMap;
  invalidateGraphics();
  reloadAllSprites();
}

void Game::applyDebugStart(bool newGame) {
  if (!newGame || debugApplied) return;
  if (opt.gold >= 0) st.gold = uint32_t(opt.gold > 999999 ? 999999 : opt.gold);
  debugApplied = true;
  for (int it : opt.items) addItem(it & 0xFF);
  if (opt.warp) {
    st.curMap = uint16_t(opt.warpMap);
    st.x = uint8_t(opt.warpX);
    st.y = uint8_t(opt.warpY);
    enterMap();
  }
}

int Game::slotSelect(int x, int y) {
  drawWindow(x, y, 120, 120);
  static const char* names[4] = {"データ１", "データ２", "データ３", "データ４"};
  for (int k = 0; k < 4; ++k) drawText(x + 32, y + 16 + 24 * k, sj(names[k]));
  return select(x, y, 4, 0, 1);
}

std::vector<uint8_t> Game::buildSave() const {
  std::vector<uint8_t> d(kSaveSize, 0);
  size_t p = 0;
  for (int i = 0; i < 12; ++i, p += 0x50) std::memcpy(&d[p], &members[i], 0x50);
  std::memcpy(&d[p], &st, sizeof(st));
  p += sizeof(st);
  std::memcpy(&d[p], inventory, sizeof(inventory));
  p += sizeof(inventory);
  std::memcpy(&d[p], switches, 1000);
  p += 1000;
  for (int i = 0; i < 100; ++i, p += 2) core::wr16(&d[p], uint16_t(vars[i]));
  core::wr16(&d[p], map.version);
  p += 2;
  p += 250;  // reserved: zeros (spec 20 §9.2 recommendation)
  for (int i = 0; i < kMaxEvents; ++i, p += 18)
    if (map.events[i]) std::memcpy(&d[p], map.events[i], 18);
  return d;
}

bool Game::saveSlot(int k) {
  const std::string path = root + "/SAVE" + std::to_string(k + 1) + ".DAT";
  return core::writeFileAbs(path, buildSave());
}

bool Game::loadSlot(int k) {
  std::vector<uint8_t> d;
  if (!core::tryReadFile(root, "SAVE" + std::to_string(k + 1) + ".DAT", d) || d.empty()) return false;
  blackOut(1);
  size_t p = 0;
  bool ok = true;
  auto take = [&](void* dst, size_t n) {
    if (!ok || p + n > d.size()) {
      ok = false;
      return false;
    }
    std::memcpy(dst, &d[p], n);
    p += n;
    return true;
  };
  for (int i = 0; i < 12; ++i) take(&members[i], 0x50);
  take(&st, sizeof(st));
  take(inventory, sizeof(inventory));
  take(switches, 1000);
  {
    uint8_t buf[200];
    if (take(buf, 200))
      for (int i = 0; i < 100; ++i) vars[i] = int16_t(core::rd16(&buf[2 * i]));
  }
  uint16_t savedVersion = 0;
  {
    uint8_t buf[2];
    if (take(buf, 2)) savedVersion = core::rd16(buf);
  }
  const size_t eventsAt = 0x0E1C;
  st.loadedMap = st.curMap;
  loadCurrentMapData();
  reapplyTone();
  reloadAllSprites();
  if (savedVersion == map.version) {
    for (int i = 0; i < kMaxEvents; ++i) {
      const size_t o = eventsAt + size_t(i) * 18;
      if (o + 18 > d.size()) break;
      if (!map.events[i]) continue;
      std::memcpy(map.events[i], &d[o], 18);
      if (map.events[i]->page1 != 9) map.events[i]->page1 = 0;
    }
  }
  recalcAll();
  enterMap();
  updatePoison();
  playBgm(st.curBgm);
  return true;
}

// ---------------------------------------------------------------------------------------------
std::unique_ptr<core::Engine> createEngine(core::Host& host, const std::string& root, const core::StartOptions& opt) {
  return std::make_unique<Game>(host, root, opt);
}

}  // namespace d2
