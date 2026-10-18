// D1 title, field, objects, movement, events (spec 11 §2, §5, §6, §7.3, §8, §12).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/text.h"
#include "d1/d1.h"
#include "d1/d1_game.h"

namespace d1 {

static const int kDX[4] = {0, 1, 0, -1};
static const int kDY[4] = {-1, 0, 1, 0};

static int blockOf(int screen) { return ((screen >> 5) / 8) * 4 + (screen & 31) / 8; }

Game::Game(core::Host& host, const std::string& root, const core::StartOptions& opt) : host_(host), opt_(opt) {
  db.load(root);
  for (int i = 0; i < 50; ++i) {
    invId[i] = kNone;
    invCnt[i] = 0;
  }
  std::memset(cells_, 0, sizeof cells_);
}

std::unique_ptr<core::Engine> createEngine(core::Host& host, const std::string& root, const core::StartOptions& opt) {
  return std::make_unique<Game>(host, root, opt);
}

// ---------------------------------------------------------------- main flow (§2)
void Game::run() {
  rng.seed(1);
  for (;;) {
    if (!opt_.noTitle) titleScreen();
    while (!titleMenu()) {
    }
    gameStart();
    if (!debugApplied_) applyDebugStart();
    if (!ended_) fieldLoop();
    fade(1, 16);
    bgmStop();
    scr_.frameBorder = false;
    ended_ = false;
  }
}

void Game::titleScreen() {
  if (!db.hasTitle) return;
  scr_.graphicsOn = false;
  scr_.frameBorder = false;
  std::memcpy(scr_.pix, db.titleIdx.data(), sizeof scr_.pix);
  scr_.setPalette(db.titlePal);
  scr_.graphicsOn = true;
  waitRelease();
  for (;;) {
    core::InputState s = in();
    if (s.ok || s.cancel) break;
    frame();
  }
}

bool Game::titleMenu() {
  scr_.clear(14);
  applyPal(6, 0);
  titleSlot_ = 0;
  for (;;) {
    window(0x1C, 5, 0x33, 8);
    MenuRes r = menu({S("最初から始める　"), S("ゲームを読み込む")}, 16, 1, 2, 0x1E, 6, 0, false);
    if (r.kind == MenuRes::Cancel) continue;
    if (r.pos == 0) {
      newGame();
      return true;
    }
    for (;;) {
      window(0x20, 8, 0x31, 0xD);
      MenuRes s = menu({S("ＤＡＴＡ１"), S("ＤＡＴＡ２"), S("ＤＡＴＡ３"), S("ＤＡＴＡ４")}, 10, 1, 4, 0x22, 9,
                       titleSlot_, false);
      if (s.kind == MenuRes::Cancel) {
        titleSlot_ = 0;
        scr_.clear(14);
        break;
      }
      titleSlot_ = s.pos;
      if (loadSlot(s.pos)) return true;
    }
  }
}

int Game::heroGraphicForParty() const { return validSlot(0) ? db.heroes[party[0]].graphic : 0; }

void Game::gameStart() {
  for (int i = 0; i < 16; ++i) scr_.setPal(i, 0, 0, 0);
  objs_[0] = Obj();
  objs_[0].event = -3;
  objs_[0].x = heroX;
  objs_[0].y = heroY;
  objs_[0].dir = 2;
  objs_[0].stage = 0;
  reloadBlock();
  redrawScreen();
  heroChr_ = vehicle == 1 ? shipGfx : vehicle == 2 ? airGfx : heroGraphicForParty();
  objs_[0].gfx = heroChr_;
  bgmChange(fieldBgm);
  scr_.frameBorder = true;
  fade(14, 0);
}

void Game::applyDebugStart() {
  debugApplied_ = true;
  if (opt_.gold >= 0) gold = std::min(opt_.gold, 99999999L);
  for (int id : opt_.items) addItem(id & 0xFF);
  if (opt_.warp) {
    curScreen = opt_.warpMap & 1023;
    objs_[0].x = std::max(0, std::min(19, opt_.warpX));
    objs_[0].y = std::max(0, std::min(11, opt_.warpY));
    reloadBlock();
    redrawScreen();
  }
  if (opt_.battle >= 0) {
    randomEncounter(opt_.battle);
    if (isGameOver()) ended_ = true;
  }
}

// ---------------------------------------------------------------- cells (§5.2)
void Game::drawCell(int x, int y, uint8_t* buf) {
  if (x < 0 || x >= 20 || y < 0 || y >= 12) return;
  uint8_t* dst = buf ? buf : scr_.pix;
  const int p = cells_[y * 20 + x];
  for (int k = 0; k < 4; ++k) {
    const core::Image& chip = db.chips[db.patChips[p][k]];
    const int ox = x * 32 + (k & 1) * 16, oy = y * 32 + (k >> 1) * 16;
    for (int yy = 0; yy < 16; ++yy) std::memcpy(dst + (oy + yy) * 640 + ox, &chip.px[yy * 16], 16);
  }
}

void Game::redrawScreen() {
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 20; ++x) drawCell(x, y);
}

void Game::reloadCells() {
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 20; ++x) cells_[y * 20 + x] = db.mapCell(curScreen, x, y);
}

void Game::reloadBlock() {
  curBlock = blockOf(curScreen);
  blockBuf_ = db.ivents[curBlock];
  reloadCells();
}

void Game::writeBackPositions(int leaving) {
  if (blockOf(leaving) != curBlock) return;
  for (int i = 1; i < objCount_; ++i) {
    const Obj& o = objs_[i];
    if (o.event < 0 || o.x == kErased) continue;
    if (o.event + 5 < int(blockBuf_.size())) {
      blockBuf_[o.event + 4] = uint8_t(o.y * 20 + o.x);
      blockBuf_[o.event + 5] = 0;
    }
  }
}

// ---------------------------------------------------------------- pages (§5.5)
bool Game::pageCond(const PageHdr& p) const {
  bool any = false;
  if (p.item < 0x80) {
    any = true;
    bool ok = hasItem(p.item);
    for (int s = 0; s < 4 && !ok; ++s)
      if (validSlot(s))
        for (int k = 0; k < 5; ++k)
          if (heroes[party[s]].equip[k] == p.item) ok = true;
    if (!ok) return false;
  }
  if (p.party != 0) {
    any = true;
    for (int n = 0; n < 8; ++n) {
      if (!(p.party & (1 << n))) continue;
      bool in = false;
      for (int s = 0; s < 4; ++s)
        if (party[s] == n) in = true;
      if (!in) return false;
    }
  }
  if (p.sw < 200) {
    any = true;
    if (!getSwitch(p.sw)) return false;
  }
  if (p.gold < 10000) {
    any = true;
    if (gold < p.gold) return false;
  }
  return any;
}

int Game::selectPage(const std::vector<uint8_t>& buf, int e, PageHdr* out) const {
  PageHdr pg[4];
  eventPages(buf, e, pg);
  int k = 0;
  for (int i = 3; i >= 1; --i)
    if (pageCond(pg[i])) {
      k = i;
      break;
    }
  if (out) *out = pg[k];
  return k;
}

// ---------------------------------------------------------------- setup (§5.4)
void Game::setup(bool first) {
  for (int i = 1; i <= 240; ++i) objs_[i].gfx = 0xFF;
  reloadCells();
  int n = 1;
  int e = 0;
  while (n <= 240 && !eventEnd(blockBuf_, e)) {
    PageHdr pg;
    selectPage(blockBuf_, e, &pg);
    if ((eventScreen(blockBuf_, e) & 0xFFFF) == curScreen) {
      Obj& o = objs_[n];
      if (first) o = Obj();
      o.event = e;
      const int pos = blockBuf_[e + 4];
      const int px = pos % 20, py = pos / 20;
      if (pg.gfx >= 0x40 && pg.gfx <= 0x62 && pg.move == 0) {
        if (first || o.x != kErased) {
          o.gfx = 0x63;
          o.x = px;
          o.y = py;
          o.dir = 2;
          o.stage = 0;
          if (py < 12 && px < 20) {
            cells_[py * 20 + px] = uint8_t(objPattern(pg.gfx));
            drawCell(px, py);
          }
        }
      } else if (first) {
        o.gfx = pg.gfx;
        o.x = px;
        o.y = py;
        o.dir = 2;
        o.stage = 0;
      } else {
        o.gfx = pg.gfx;
      }
      o.move = pg.move;
      o.trig = pg.trig;
      ++n;
    }
    e = nextEvent(blockBuf_, e);
  }
  auto placeVehicle = [&](int ev, int gfx, int pos) {
    if (n > 240) return;
    Obj& o = objs_[n];
    o = Obj();
    o.event = ev;
    o.gfx = gfx;
    o.x = pos % 20;
    o.y = pos / 20;
    o.dir = 2;
    o.stage = 0;
    o.move = 0;
    o.trig = 0;
    ++n;
  };
  if (vehicle != 1 && shipScreen == curScreen) placeVehicle(-1, shipGfx, shipPos);
  if (vehicle != 2 && airScreen == curScreen) placeVehicle(-2, airGfx, airPos);
  objCount_ = n;
}

// ---------------------------------------------------------------- objects (§5.8)
void Game::drawObj(int i, bool advance) {
  Obj& o = objs_[i];
  if (o.x == kErased) return;
  const int dx = o.stage ? kDX[o.dir] : 0, dy = o.stage ? kDY[o.dir] : 0;
  if (i != 0 && o.gfx == 0x63) {
    if (!(advance || (o.x == objs_[0].x && o.y == objs_[0].y))) drawCell(o.x, o.y);
  } else {
    drawCell(o.x, o.y);
    if (o.stage) drawCell(o.x + kDX[o.dir], o.y + kDY[o.dir]);
    const int px = o.x * 32 + dx * o.stage * 8, py = o.y * 32 + dy * o.stage * 8;
    core::ClipRect clip{0, 0, 640, 384};
    if (i == 0) {
      if (heroChr_ >= 0 && heroChr_ < 64 && db.chrOk[heroChr_])
        scr_.blit(db.chr[heroChr_][o.dir * 2 + anim_], px, py, true, clip);
    } else if (o.gfx >= 0x40 && o.gfx <= 0x62) {
      const int p = objPattern(o.gfx);
      for (int k = 0; k < 4; ++k)
        scr_.blit(db.chips[db.patChips[p][k]], px + (k & 1) * 16, py + (k >> 1) * 16, true, clip);
    } else if (i == evObj_ && evChrLoaded_) {
      if (evChr_ < 64 && db.chrOk[evChr_]) scr_.blit(db.chr[evChr_][o.dir * 2 + anim_], px, py, true, clip);
    } else if (o.gfx < 64) {
      scr_.blit(anim_ == 0 ? db.chrA[o.gfx] : db.chrB[o.gfx], px, py, true, clip);
    }
  }
  if (advance && o.stage > 0) {
    ++o.stage;
    if (o.stage == 5) {
      o.stage = 0;
      o.x += kDX[o.dir];
      o.y += kDY[o.dir];
    }
  }
}

void Game::drawAllObjs() {
  for (int i = 0; i < objCount_; ++i) drawObj(i, false);
}

// ---------------------------------------------------------------- passability (§5.6)
bool Game::passable(int x, int y, int mode) const {
  if (x < 0 || x >= 20 || y < 0 || y >= 12) return false;
  if (mode == 2) return true;
  const int p = cells_[y * 20 + x];
  if (db.blocked(p)) return false;
  return db.land(p) ? mode == 0 : mode == 1;
}

bool Game::landable(int x, int y) const {
  if (x < 0 || x >= 20 || y < 0 || y >= 12) return false;
  const int p = cells_[y * 20 + x];
  return !db.blocked(p) && db.land(p);
}

bool Game::onWater(int x, int y) const {
  if (x < 0 || x >= 20 || y < 0 || y >= 12) return false;
  return !db.land(cells_[y * 20 + x]);
}

// ---------------------------------------------------------------- movement (§5.11)
MoveResult Game::tryMove(int i, int d, int mode, int* blocker) {
  Obj& o = objs_[i];
  if (o.stage != 0) return MoveResult::Moving;
  o.dir = d;
  const int tx = o.x + kDX[d], ty = o.y + kDY[d];
  if (tx < 0 || tx >= 20 || ty < 0 || ty >= 12) return MoveResult::Edge;
  const bool pass = passable(tx, ty, mode);
  for (int s = 0; s < objCount_; ++s) {
    if (s == i) continue;
    const Obj& so = objs_[s];
    if (so.x == kErased) continue;
    const bool stoppedAt = so.x == tx && so.y == ty && so.stage == 0;
    if (i == 0) {
      if (so.gfx == 0x63) {
        const int terr = onWater(tx, ty) ? 1 : 0;
        if (so.trig == 1 && stoppedAt && !pass && terr == mode) {
          if (blocker) *blocker = s;
          return MoveResult::BlockedStopped;
        }
        continue;
      }
      if (so.gfx >= 0x40 && so.move == 0) continue;
    }
    const bool occupies = so.stage != 0 && so.x + kDX[so.dir] == tx && so.y + kDY[so.dir] == ty;
    if (stoppedAt) {
      if (blocker) *blocker = s;
      return MoveResult::BlockedStopped;
    }
    if (occupies) {
      if (blocker) *blocker = s;
      return MoveResult::BlockedMoving;
    }
  }
  if (!pass) return MoveResult::Terrain;
  o.stage = 1;
  return MoveResult::Success;
}

void Game::randomMove(int i, int mode) {
  if (rng.next() > 10000) return;
  const int d0 = rng.next() % 4;
  for (int k = 0; k < 4; ++k)
    if (tryMove(i, (d0 + k) % 4, mode, nullptr) == MoveResult::Success) break;
}

// ---------------------------------------------------------------- NPC update (§5.9)
bool Game::npcUpdate() {
  const bool judge = uint32_t(frameCounter_ - lastJudge_) > 32;
  if (judge) lastJudge_ = frameCounter_;
  for (int i = 1; i < objCount_; ++i) {
    Obj& o = objs_[i];
    if (o.x == kErased) continue;
    drawObj(i, true);
    if (!judge || o.stage != 0) continue;
    const int mode = onWater(o.x, o.y) ? 1 : 0;
    switch (o.move) {
      case 1:
        randomMove(i, mode);
        break;
      case 2: {
        if (o.dir == 0 || o.dir == 2) o.dir = 1;
        if (tryMove(i, o.dir, mode, nullptr) != MoveResult::Success) o.dir = o.dir == 1 ? 3 : 1;
        break;
      }
      case 3: {
        if (o.dir == 1 || o.dir == 3) o.dir = 0;
        if (tryMove(i, o.dir, mode, nullptr) != MoveResult::Success) o.dir = o.dir == 0 ? 2 : 0;
        break;
      }
      case 4: {
        const int dx = objs_[0].x - o.x;
        int cand = dx > 0 ? 1 : 3;
        if (dx == 0 || rng.next() < 16000) {
          const int dy = objs_[0].y - o.y;
          cand = dy > 0 ? 2 : 0;
        }
        int blk = -1;
        MoveResult r = tryMove(i, cand, mode, &blk);
        if (r == MoveResult::BlockedStopped && blk == 0 && o.trig == 1) {
          runEvent(i);
          return true;
        }
        if (r != MoveResult::Success) randomMove(i, mode);
        break;
      }
      case 5: {
        const int dx = objs_[0].x - o.x, dy = objs_[0].y - o.y;
        if (dx > 0 && tryMove(i, 3, mode, nullptr) == MoveResult::Success) break;
        if (dx < 0 && tryMove(i, 1, mode, nullptr) == MoveResult::Success) break;
        if (dy >= 0 && tryMove(i, 0, mode, nullptr) == MoveResult::Success) break;
        if (dy <= 0 && tryMove(i, 2, mode, nullptr) == MoveResult::Success) break;
        randomMove(i, mode);
        break;
      }
      default:
        break;
    }
  }
  return false;
}

// ---------------------------------------------------------------- field loop (§6.1, §6.2)
int Game::findStandingObj() const {
  for (int i = 1; i < objCount_; ++i) {
    const Obj& o = objs_[i];
    if (o.x != kErased && o.x == objs_[0].x && o.y == objs_[0].y && o.stage == 0) return i;
  }
  return -1;
}

void Game::touch(int s) {
  lastTouched_ = s;
  if (objs_[s].trig == 1) runEvent(s);
}

void Game::fieldLoop() {
  bool first = true;
  uint64_t mark = ~uint64_t(0);
  for (;;) {
    if (mark == totalFrames_) frame();
    mark = totalFrames_;
    setup(first);
    first = false;
    drawAllObjs();
    bool ran = false;
    for (int i = 1; i < objCount_; ++i) {
      if (objs_[i].x != kErased && objs_[i].trig == 2) {
        runEvent(i);
        ran = true;
        break;
      }
    }
    if (ran) {
      if (ended_) return;
      continue;
    }
    for (;;) {
      FP r = frameProc();
      if (r == FP::Exit) return;
      if (r == FP::Continue) continue;
      if (ended_) return;
      first = (r == FP::RestartFirst);
      break;
    }
  }
}

Game::FP Game::frameProc() {
  const uint32_t t0 = frameCounter_;
  Obj& h = objs_[0];
  if (h.stage == 0 && vehicle != 2) {
    const int s = findStandingObj();
    if (s >= 0 && s != lastTouched_) {
      touch(s);
      return FP::Restart;
    }
  }
  const core::InputState s = in();
  if (s.ok && h.stage == 0) {
    examine();
    return ended_ ? FP::Exit : FP::Restart;
  }
  if (s.cancel && vehicle == 0 && h.stage == 0) {
    se(15);
    MM r = mainMenu();
    if (r == MM::Quit) {
      ended_ = true;
      return FP::Exit;
    }
    if (r == MM::Reload) return FP::RestartFirst;
    return FP::Continue;
  }
  const int d = dirKey(s);
  if (h.stage == 0 && d >= 0) {
    int blk = -1;
    MoveResult r = tryMove(0, d, vehicle, &blk);
    if (r == MoveResult::Success) {
      lastTouched_ = -1;
    } else if (r == MoveResult::BlockedStopped) {
      Obj& b = objs_[blk];
      if (b.gfx >= 0x3A && b.gfx <= 0x3D) {
        const int k = b.gfx - 0x3A;
        bool key = false;
        for (int i = 0; i < 50; ++i)
          if (invId[i] < 0x80 && db.items[invId[i]].kind == 6 && db.items[invId[i]].value == k) key = true;
        if (key) {
          if (b.event >= 0 && b.event + 7 < int(blockBuf_.size())) blockBuf_[b.event + 7] = 0x63;
          b.gfx = 0x63;
          se(24);
          drawObj(blk, false);
          return FP::Continue;
        }
      }
      touch(blk);
      return ended_ ? FP::Exit : FP::Restart;
    } else if (r == MoveResult::Edge) {
      drawObj(0, false);
      const int old = curScreen;
      if (switchScreen(d)) {
        h.x -= kDX[d] * 19;
        h.y -= kDY[d] * 11;
        writeBackPositions(old);
        lastTouched_ = -1;
        return FP::RestartFirst;
      }
    }
  }
  if (npcUpdate()) return ended_ ? FP::Exit : FP::Restart;
  if (ended_) return FP::Exit;
  const int p = h.stage;
  drawObj(0, true);
  if (p == 4 && vehicle == 0) {
    bool battled = false;
    encounterCheck(&battled);
    if (battled) {
      if (isGameOver()) {
        ended_ = true;
        return FP::Exit;
      }
      return FP::Continue;
    }
    walkPoison();
  }
  while (uint32_t(frameCounter_ - t0) < 3) frame();
  return FP::Continue;
}

// ---------------------------------------------------------------- examine & vehicles (§6.3, §6.5)
void Game::examine() {
  const int tx = objs_[0].x + kDX[objs_[0].dir], ty = objs_[0].y + kDY[objs_[0].dir];
  if (vehicle != 0) {
    disembark(tx, ty);
    return;
  }
  for (int i = objCount_ - 1; i >= 1; --i) {
    const Obj& o = objs_[i];
    if (o.x != kErased && o.x == tx && o.y == ty && o.stage == 0) {
      if (o.event == -1 || o.event == -2)
        board(tx, ty);
      else
        runEvent(i);
      return;
    }
  }
  const int s = findStandingObj();
  if (s >= 0) runEvent(s);
}

void Game::board(int tx, int ty) {
  int v = -1;
  for (int i = objCount_ - 1; i >= 1; --i) {
    const Obj& o = objs_[i];
    if ((o.event == -1 || o.event == -2) && o.x == tx && o.y == ty) {
      v = i;
      break;
    }
  }
  if (v < 0) return;
  fade(1, 15);
  vehicle = objs_[v].event == -1 ? 1 : 2;
  bgmChange(vehicle == 1 ? shipBgm : airBgm);
  objs_[0].x = objs_[v].x;
  objs_[0].y = objs_[v].y;
  heroChr_ = objs_[v].gfx;
  vehicleCommon();
}

bool Game::disembark(int tx, int ty) {
  if (!landable(tx, ty)) return false;
  for (int i = 1; i < objCount_; ++i) {
    const Obj& o = objs_[i];
    if (o.x != tx || o.y != ty) continue;
    if (o.gfx == 0x63) continue;
    if (o.gfx >= 0x40 && o.move == 0) continue;
    return false;
  }
  fade(1, 15);
  bgmChange(fieldBgm);
  const int pos = objs_[0].y * 20 + objs_[0].x;
  if (vehicle == 1) {
    shipScreen = curScreen;
    shipPos = pos;
  } else {
    airScreen = curScreen;
    airPos = pos;
  }
  heroChr_ = heroGraphicForParty();
  objs_[0].x = tx;
  objs_[0].y = ty;
  vehicle = 0;
  vehicleCommon();
  return true;
}

void Game::vehicleCommon() {
  writeBackPositions(curScreen);
  redrawScreen();
  setup(true);
  drawAllObjs();
  fade(15, 0);
}

// ---------------------------------------------------------------- screen switch (§6.4)
bool Game::inRing(int X, int Y) const {
  const int w = (ring % 4 + 1) * 8, hgt = (ring / 4 + 1) * 8;
  return X < w && Y < hgt;
}

bool Game::switchScreen(int d) {
  const int X = curScreen & 31, Y = curScreen >> 5;
  int X2 = X + kDX[d], Y2 = Y + kDY[d];
  if (inRing(X, Y)) {
    const int w = (ring % 4 + 1) * 8, hgt = (ring / 4 + 1) * 8;
    X2 = (X2 + w) % w;
    Y2 = (Y2 + hgt) % hgt;
  } else {
    if (X2 < 0 || X2 > 31 || Y2 < 0 || Y2 > 31 || inRing(X2, Y2)) return false;
  }
  uint8_t saved[240];
  std::memcpy(saved, cells_, 240);
  const int oldScreen = curScreen, oldBlock = curBlock;
  curScreen = Y2 * 32 + X2;
  curBlock = blockOf(curScreen);
  reloadCells();
  const int ax = objs_[0].x - kDX[d] * 19, ay = objs_[0].y - kDY[d] * 11;
  if (ax < 0 || ax >= 20 || ay < 0 || ay >= 12 || !passable(ax, ay, vehicle)) {
    curScreen = oldScreen;
    curBlock = oldBlock;
    std::memcpy(cells_, saved, 240);
    return false;
  }
  if (curBlock != oldBlock) blockBuf_ = db.ivents[curBlock];
  std::vector<uint8_t> oldPix(scr_.pix, scr_.pix + 640 * 384);
  std::vector<uint8_t> newPix(640 * 400, 0);
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 20; ++x) drawCell(x, y, newPix.data());
  const bool vert = d == 0 || d == 2;
  const int N = vert ? 24 : 40;
  for (int k = 1; k <= N; ++k) {
    const int s = 16 * k;
    for (int y = 0; y < 384; ++y) {
      uint8_t* dst = scr_.pix + y * 640;
      if (d == 0) {
        std::memcpy(dst, y < s ? &newPix[(y + 384 - s) * 640] : &oldPix[(y - s) * 640], 640);
      } else if (d == 2) {
        std::memcpy(dst, y >= 384 - s ? &newPix[(y - (384 - s)) * 640] : &oldPix[(y + s) * 640], 640);
      } else if (d == 1) {
        for (int x = 0; x < 640; ++x) dst[x] = x >= 640 - s ? newPix[y * 640 + x - (640 - s)] : oldPix[y * 640 + x + s];
      } else {
        for (int x = 0; x < 640; ++x) dst[x] = x < s ? newPix[y * 640 + x + 640 - s] : oldPix[y * 640 + x - s];
      }
    }
    frame();
  }
  std::memcpy(scr_.pix, newPix.data(), 640 * 384);
  return true;
}

// ---------------------------------------------------------------- encounters (§6.7-6.9)
void Game::encounterCheck(bool* battled) {
  *battled = false;
  --encCounter_;
  if (encCounter_ < 0) encCounter_ = rng.next() % 16 + 16;
  const int g = db.enemyMap[curScreen & 1023];
  if (g < 0x20 && encCounter_ == 0) {
    randomEncounter(g);
    *battled = true;
  }
}

void Game::walkPoison() {
  bool any = false;
  for (int s = 0; s < 4; ++s) {
    if (!validSlot(s)) continue;
    Hero& h = heroes[party[s]];
    if (!(h.status & 1)) continue;
    const int dmg = std::max(1, std::min(10, h.recMaxHp / 30));
    h.hp -= dmg;
    if (h.hp <= 0) h.hp = 1;
    any = true;
  }
  if (any) {
    applyPal(palMode, -8);
    waitMs(20);
    applyPal(palMode, 0);
  }
}

void Game::randomEncounter(int group) {
  bgmChange(battleBgm);
  for (int k = 0; k < 3; ++k) {
    applyPal(palMode, -6);
    waitMs(50);
    applyPal(palMode, 0);
    waitMs(50);
  }
  for (int r = 0; r < 192; ++r) {
    scr_.fillRect(0, r, 640, 1, 14);
    scr_.fillRect(0, 383 - r, 640, 1, 14);
    if (r % 9 == 8) frame();
  }
  battle(group, false);
}

// ---------------------------------------------------------------- message window (§7.3)
void Game::msgOpen() {
  window(0x10, 1, 0x3F, 5);
  mY0_ = 2;
  mH_ = 4;
  msgClear();
  msgOpen_ = true;
}

void Game::msgClose() {
  if (!msgOpen_) return;
  for (int y = 0; y <= 4; ++y)
    for (int x = 4; x <= 15; ++x) drawCell(x, y);
  drawAllObjs();
  msgOpen_ = false;
  mY0_ = 1;
}

void Game::eventYesNo(bool* yes) {
  *yes = yesNo(0x22, 7);
  msgClose();
  for (int y = 5; y <= 8; ++y)
    for (int x = 8; x <= 11; ++x) drawCell(x, y);
  drawAllObjs();
}

// ---------------------------------------------------------------- events (§8)
bool Game::runEvent(int o) {
  drawObj(0, false);
  lastTouched_ = o;
  if (objs_[o].event < 0) return true;
  const std::vector<uint8_t> cp = blockBuf_;
  const int E = objs_[o].event;
  PageHdr pg;
  selectPage(cp, E, &pg);
  int pc = pg.start + 10;
  msgOpen_ = false;
  bool yes = false, bgmSet = false;
  evChrLoaded_ = false;
  evObj_ = o;
  const int startScreen = curScreen;
  const int n = int(cp.size());
  auto atEnd = [&](int p) { return p + 1 >= n || (cp[p] == 0xFF && cp[p + 1] == 0xFF); };
  auto skipRest = [&]() {
    while (!atEnd(pc)) pc += cmdLen(cp, pc);
  };
  auto abort = [&]() {
    evObj_ = -1;
    evChrLoaded_ = false;
    return false;
  };
  while (!atEnd(pc)) {
    const int op = cp[pc];
    if (opt_.verbose) {
      char b[48];
      std::snprintf(b, sizeof b, "[d1 op %02X]", op);
      host_.log(b);
    }
    switch (op) {
      case 0x2A: {
        const int len = cp[pc + 1];
        std::string text;
        for (int k = 0; k < len && pc + 2 + k < n; ++k) {
          if (cp[pc + 2 + k] == 0) break;
          text.push_back(char(cp[pc + 2 + k]));
        }
        msgOpen();
        msgPut(text);
        waitRelease();
        waitPress();
        pc += 2 + len;
        break;
      }
      case 0x3F: {
        bool y;
        eventYesNo(&y);
        pc += 1;
        if (y) {
          yes = true;
        } else {
          while (!atEnd(pc)) {
            if (cp[pc] == 0x3A) {
              pc += 1;
              break;
            }
            pc += cmdLen(cp, pc);
          }
        }
        break;
      }
      case 0x3A:
        if (yes)
          skipRest();
        else
          pc += 1;
        break;
      case 0x62: {
        const int v = cp[pc + 1];
        if (v < 100) {
          fieldBgm = v;
          bgmChange(v);
          bgmSet = true;
        } else {
          se(v - 90);
        }
        pc += 2;
        break;
      }
      case 0x63: {
        bool neg;
        const int v = signedArg(&cp[pc + 1], &neg);
        if (!neg) {
          for (int s = 0; s < 4; ++s) {
            if (party[s] == v) break;
            if (party[s] >= 8) {
              party[s] = uint8_t(v);
              break;
            }
          }
        } else {
          for (int s = 0; s < 4; ++s) {
            if (party[s] == v) {
              for (int k = s; k < 3; ++k) party[k] = party[k + 1];
              party[3] = 0xFF;
              break;
            }
          }
        }
        if (validSlot(0)) {
          msgClose();
          heroChr_ = db.heroes[party[0]].graphic;
          drawObj(0, false);
        }
        pc += 3;
        break;
      }
      case 0x64:
        for (int s = 0; s < 4; ++s)
          if (validSlot(s) && heroes[party[s]].hp != 0) heroes[party[s]].status |= 1;
        pc += 1;
        break;
      case 0x65: {
        const int k = cp[pc + 1] - '0';
        switch (k) {
          case 0:
            for (int v = 1; v <= 12; ++v) {
              applyPal(palMode, v == 12 ? 0 : -v);
              waitMs(30);
            }
            break;
          case 1: palMode = 4; applyPal(palMode, 0); break;
          case 2: palMode = 5; applyPal(palMode, 0); break;
          case 3: palMode = 1; applyPal(palMode, 0); break;
          case 4: palMode = 2; applyPal(palMode, 0); break;
          case 5: palMode = 3; applyPal(palMode, 0); break;
          case 6: palMode = 6; applyPal(palMode, 0); break;
          case 7:
            se(22);
            for (int r = 0; r < 3; ++r) {
              scr_.hOffset = 1;
              waitMs(40);
              scr_.hOffset = 0;
              waitMs(40);
            }
            break;
          case 8:
            waitMs(1000);
            break;
          case 9: {
            if ((eventScreen(cp, E) & 0xFFFF) != curScreen) break;
            Obj& ob = objs_[o];
            // Spec 11 §8.2 0x65 '9': the window is closed even if the object was already erased
            // (an erased object has x = 0xFF, so the cell steps touch nothing).
            if (ob.gfx >= 0x40 && ob.move == 0 && ob.x < 20 && ob.y < 12)
              cells_[ob.y * 20 + ob.x] = db.mapCell(curScreen, ob.x, ob.y);
            msgClose();
            drawCell(ob.x, ob.y);
            ob.x = kErased;
            break;
          }
          default:
            break;
        }
        pc += 2;
        break;
      }
      case 0x66: {
        bool neg;
        const int v = signedArg(&cp[pc + 1], &neg);
        if (v < 200) setSwitch(v, !neg);
        pc += 3;
        break;
      }
      case 0x67: {
        bool neg;
        const int v = signedArg(&cp[pc + 1], &neg);
        gold += neg ? -v : v;
        gold = std::max(0L, std::min(gold, 99999999L));
        pc += 3;
        break;
      }
      case 0x68: {
        bool neg;
        const int v = signedArg(&cp[pc + 1], &neg);
        const int delta = neg ? -v : v;
        for (int s = 0; s < 4; ++s) {
          if (!validSlot(s)) continue;
          Hero& hh = heroes[party[s]];
          if (hh.hp == 0) continue;
          hh.recMaxHp = calcMaxHp(party[s]);
          hh.hp = std::max(0, std::min(hh.hp + delta, hh.recMaxHp));
        }
        if (isGameOver()) {
          msgOpen();
          msgPut(S("全滅してしまった‥‥_"));
          ended_ = true;
          return abort();
        }
        pc += 3;
        break;
      }
      case 0x69: {
        bool neg;
        const int v = signedArg(&cp[pc + 1], &neg);
        pc += 3;
        if (!neg) {
          if (!addItem(v)) {
            msgOpen();
            msgPut(S("持物がいっぱいです。_"));
            skipRest();
          }
        } else {
          removeItem(v, false);
        }
        break;
      }
      case 0x6A: {
        writeBackPositions(curScreen);
        const int scrn = (cp[pc + 1] | (cp[pc + 2] << 8)) & 1023;
        const int pos = cp[pc + 3];
        curScreen = scrn;
        objs_[0].x = pos % 20;
        objs_[0].y = pos / 20;
        if (blockOf(curScreen) == curBlock)
          reloadCells();
        else
          reloadBlock();
        fade(1, 16);
        redrawScreen();
        setup(true);
        drawAllObjs();
        fade(15, 0);
        pc += 4;
        break;
      }
      case 0x6D: {
        bool neg;
        const int v = signedArg(&cp[pc + 1], &neg);
        const int delta = neg ? -v : v;
        for (int s = 0; s < 4; ++s) {
          if (!validSlot(s)) continue;
          Hero& hh = heroes[party[s]];
          hh.recMaxMp = calcMaxMp(party[s]);
          hh.mp = std::max(0, std::min(hh.mp + delta, hh.recMaxMp));
        }
        pc += 3;
        break;
      }
      case 0x6E: {
        const int fee = signedArg(&cp[pc + 1], nullptr);
        pc += 3;
        if (fee > 0) {
          msgOpen();
          msgPut(S("#宿泊料金は"));
          msgNum(fee);
          msgPut(S(" です。泊まりますか？"));
          bool y;
          eventYesNo(&y);
          if (!y) break;
          if (fee > gold) {
            msgOpen();
            msgClear();
            msgPut(S("お金が足りません。_"));
            break;
          }
          gold -= fee;
          bgmChange(36);
          fade(1, 16);
          waitMs(3000);
          fade(15, 0);
          bgmChange(fieldBgm);
        }
        for (int s = 0; s < 4; ++s) {
          if (!validSlot(s)) continue;
          const int c = party[s];
          Hero& hh = heroes[c];
          if (hh.hp > 0 && (hh.status & 3)) continue;
          hh.recMaxHp = hh.hp = calcMaxHp(c);
          hh.recMaxMp = hh.mp = calcMaxMp(c);
        }
        break;
      }
      case 0x70: {
        for (int s = 0; s < 4; ++s) {
          if (!validSlot(s)) continue;
          Hero& hh = heroes[party[s]];
          if (hh.hp != 0 && !(hh.status & 2)) {
            hh.status |= 2;
            break;
          }
        }
        if (isGameOver()) {
          msgOpen();
          msgPut(S("全滅してしまった‥‥_"));
          ended_ = true;
          return abort();
        }
        pc += 1;
        break;
      }
      case 0x71:
        ending();
        ended_ = true;
        return abort();
      case 0x73: {
        uint8_t args[4];
        for (int k = 0; k < 4; ++k) args[k] = pc + 1 + k < n ? cp[pc + 1 + k] : 0x80;
        msgOpen();
        shop(args);
        redrawScreen();
        msgClose();
        pc += 5;
        break;
      }
      case 0x74:
        warpScreen = (cp[pc + 1] | (cp[pc + 2] << 8)) & 1023;
        warpPos = cp[pc + 3];
        warpPal = palMode;
        warpBgm = fieldBgm;
        pc += 4;
        break;
      case 0x76: {
        const int k = cp[pc + 1] - '0';
        pc += 2;
        if (o >= objCount_) break;
        Obj& ob = objs_[o];
        if (ob.gfx >= 0x40 && ob.move == 0) break;
        if (k < 0 || k > 7) break;  // not '0'-'7': do nothing, window stays open (11 §8.2, v1.1)
        msgClose();
        while (ob.stage != 0) drawObj(o, true);
        if (ob.gfx < 0x40 && !evChrLoaded_) {
          evChr_ = ob.gfx;
          evChrLoaded_ = true;
        }
        if (k < 4) {
          ob.dir = k;
          drawObj(o, false);
          waitMs(120);
        } else {
          const int mode = onWater(ob.x, ob.y) ? 1 : 0;
          tryMove(o, k - 4, mode, nullptr);
          for (int r = 0; r < 4; ++r) {
            drawObj(o, true);
            frame();
          }
        }
        break;
      }
      case 0x77: {
        const int k = cp[pc + 1] - '0';
        pc += 2;
        if (k < 0 || k > 7) break;  // not '0'-'7': do nothing, window stays open (11 §8.2, v1.1)
        msgClose();
        Obj& h = objs_[0];
        if (k < 4) {
          h.dir = k;
          drawObj(0, false);
          waitMs(120);
          break;
        }
        const int d = k - 4;
        const int tx = h.x + kDX[d], ty = h.y + kDY[d];
        int blk = -1;
        MoveResult r = tryMove(0, d, vehicle, &blk);
        if (r == MoveResult::BlockedStopped && (objs_[blk].event == -1 || objs_[blk].event == -2)) {
          if (vehicle == 0)
            board(tx, ty);
          else
            disembark(tx, ty);
        } else if (vehicle == 1 && landable(tx, ty)) {
          disembark(tx, ty);
        } else if (r != MoveResult::Edge) {
          for (int q = 0; q < 4; ++q) {
            drawObj(0, true);
            frame();
          }
        } else {
          drawObj(0, false);
          const int old = curScreen;
          if (switchScreen(d)) {
            h.x -= kDX[d] * 19;
            h.y -= kDY[d] * 11;
            writeBackPositions(old);
            setup(true);
            drawAllObjs();
          }
        }
        break;
      }
      case 0x78: {
        const int v = signedArg(&cp[pc + 1], nullptr);
        for (int s = 0; s < 4; ++s) addExp(s, v);
        pc += 3;
        break;
      }
      case 0x79: {
        msgClose();
        if (!bgmSet) bgmChange(battleBgm);
        const int res = battle(cp[pc + 1], true);
        pc += 2;
        if (isGameOver()) {
          ended_ = true;
          return abort();
        }
        if (res == -1)
          skipRest();
        else
          drawAllObjs();
        break;
      }
      default:
        pc += 1;
        break;
    }
  }
  msgClose();
  waitRelease();
  if (curScreen != startScreen) lastTouched_ = -1;
  evObj_ = -1;
  evChrLoaded_ = false;
  return true;
}

// ---------------------------------------------------------------- shop (§8.3, §8.4)
static std::string fmtS(const char* f, long v) {
  char b[64];
  std::snprintf(b, sizeof b, f, v);
  return b;
}

void Game::shop(const uint8_t* args) {
  std::vector<int> goods;
  for (int k = 0; k < 4; ++k) {
    if (args[k] >= 0x80) break;
    goods.push_back(args[k]);
  }
  const int n = int(goods.size());
  for (;;) {
    window(0x16, 7, 0x26, 0xA);
    MenuRes r = menu({S("品物を買う"), S("持物を売る")}, 10, 1, 2, 0x18, 8, 0, false);
    if (r.kind == MenuRes::Cancel) return;
    if (r.pos == 0) {
      window(0x1C, 7, 0x36, 8 + n);
      int mem = 0;
      std::vector<std::string> names;
      for (int g : goods) names.push_back(core::rawName(db.items[g].name, 16) + "  " + fmtS("%4ld", db.items[g].price));
      for (;;) {
        msgClear();
        goldWindow();
        if (n == 0) break;
        MenuRes m = menu(names, 0x16, 1, n, 0x1E, 8, mem, false);
        if (m.kind == MenuRes::Cancel) break;
        mem = m.pos;
        const int it = goods[mem];
        if (db.items[it].price > gold) {
          msgPut(S("お金が足りません。_"));
          continue;
        }
        if (!addItem(it)) {
          msgPut(S("持物がいっぱいです。_"));
          continue;
        }
        msgPut(itemName(it));
        msgPut(S("を購入しました_"));
        gold = std::max(0L, std::min(99999999L, gold - db.items[it].price));
      }
    } else {
      for (;;) {
        msgClear();
        goldWindow();
        const int id = sellSelect();
        if (id < 0) break;
        const int price = db.items[id].price;
        if (price == 0) {
          msgPut(S("この品物は買い取れません。_"));
          continue;
        }
        const int sv = price == 1 ? 1 : price / 2;
        msgPut(S("ひとつ"));
        msgNum(sv);
        msgPut(S(" になります。#よろしいですか？#"));
        if (yesNo(0x22, 7)) {
          gold = std::max(0L, std::min(99999999L, gold + sv));
          removeItem(id, false);
          msgPut(itemName(id));
          msgPut(S("を売りました_"));
        }
      }
    }
  }
}

int Game::sellSelect() {
  window(0xC, 7, 0x43, 0xC);
  int top = (invCursor_ / 10) * 10, pos = invCursor_ % 10;
  for (;;) {
    drawStr(0xF, 7, S("← Page ") + fmtS("%-2ld", top / 5 + 1) + S("→"));
    std::vector<std::string> items;
    for (int i = 0; i < 10; ++i) {
      const int sl = (top + i) % 50;
      if (invId[sl] >= 0x80)
        items.push_back(S("　　　　　　　　　　　"));
      else
        items.push_back(core::rawName(db.items[invId[sl]].name, 16) + S(" ×") + fmtS("%2ld", invCnt[sl]));
    }
    MenuRes r = menu(items, 0x16, 2, 5, 0xD, 8, pos, true);
    if (r.kind == MenuRes::Cancel) return -1;
    if (r.kind == MenuRes::Next) {
      top = (top + 5) % 50;
      pos = r.pos;
      continue;
    }
    if (r.kind == MenuRes::Prev) {
      top = (top + 45) % 50;
      pos = r.pos;
      continue;
    }
    const int sl = (top + r.pos) % 50;
    if (invId[sl] >= 0x80) {
      pos = r.pos;
      continue;
    }
    invCursor_ = sl;
    return invId[sl];
  }
}

// ---------------------------------------------------------------- ending (§12.1)
void Game::ending() {
  scr_.fillRect(0, 0, 640, 16, 15);
  scr_.fillRect(0, 368, 640, 16, 15);
  scr_.fillRect(0, 0, 16, 384, 15);
  scr_.fillRect(624, 0, 16, 384, 15);
  scr_.frameBorder = false;
  fade(1, 15);
  scr_.clear(14);
  static const uint8_t fin[24][6] = {
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x3F, 0xFC, 0x00, 0x00, 0x00}, {0x03, 0xFF, 0xF8, 0x00, 0x00, 0x00},
      {0x04, 0x00, 0x30, 0x00, 0x00, 0x00}, {0x08, 0x00, 0x60, 0x00, 0x00, 0x00}, {0x08, 0x00, 0x60, 0x00, 0x00, 0x00},
      {0x04, 0x00, 0xC0, 0x00, 0x00, 0x00}, {0x03, 0x00, 0xC0, 0x00, 0x00, 0x00}, {0x01, 0x80, 0xC0, 0x00, 0x00, 0x00},
      {0x00, 0x01, 0x80, 0x00, 0x00, 0x00}, {0x00, 0x01, 0x81, 0x80, 0x00, 0x00}, {0x00, 0x1F, 0xE1, 0x80, 0x00, 0x00},
      {0x00, 0x03, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x03, 0x02, 0x06, 0x00, 0x00}, {0x00, 0x03, 0x03, 0x02, 0x78, 0x00},
      {0x00, 0x06, 0x03, 0x03, 0x98, 0x00}, {0x00, 0x06, 0x06, 0x06, 0x10, 0x00}, {0x00, 0x06, 0x06, 0x04, 0x30, 0x00},
      {0x00, 0x04, 0x06, 0x04, 0x30, 0x00}, {0x0F, 0x04, 0x0C, 0x08, 0x60, 0x00}, {0x07, 0xC8, 0x0D, 0x88, 0x68, 0xC0},
      {0x00, 0x70, 0x07, 0x08, 0x70, 0xC0}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  for (int r = 0; r < 24; ++r)
    for (int b = 0; b < 6; ++b) scr_.bitmap1(&fin[r][b], 1, 296 + b * 8, 180 + r, 15);
  fade(14, 0);
  waitMs(1000);
  waitRelease();
  for (;;) {
    core::InputState s = in();
    if (s.ok || s.cancel) break;
    frame();
  }
}

}  // namespace d1
