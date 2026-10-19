// D1 main menu, items, magic, equipment, status, sort, system (spec 11 §9).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/text.h"
#include "d1/d1_game.h"

namespace d1 {

static std::string fmt(const char* f, long v) {
  char b[64];
  std::snprintf(b, sizeof b, f, v);
  return b;
}

static std::string fmt2(const char* f, long a, long b) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, a, b);
  return buf;
}

static const char* kBlank11 = "　　　　　　　　　　　";

// ---------------------------------------------------------------- main menu (§9.1)
Game::MM Game::mainMenu() {
  scr_.frameBorder = false;
  int mem = 0;
  bool statusShown = false;
  bool full = true;
  for (;;) {
    if (full) {
      scr_.clear(14);
      goldWindow();
      partyWindow();
    }
    full = true;
    window(4, 1, 0x13, 8);
    MenuRes r = menu({S("アイテム　"), S("魔法　　　"), S("装備　　　"), S("ステータス"), S("並び替え　"), S("システム　")},
                     10, 1, 6, 6, 2, mem, false);
    if (r.kind == MenuRes::Selected) mem = r.pos;
    if (r.kind == MenuRes::Selected && (r.pos == 1 || r.pos == 3 || r.pos == 4) && statusShown) {
      partyWindow();
      statusShown = false;
    }
    if (r.kind == MenuRes::Cancel) {
      scr_.frameBorder = true;
      redrawScreen();
      waitRelease();
      return MM::Normal;
    }
    switch (r.pos) {
      case 0:
        if (itemMenu()) return MM::Reload;
        break;
      case 1:
        if (magicMenu()) return MM::Reload;
        break;
      case 2:
        equipMenu();
        break;
      case 3: {
        const int s = heroSelect(0, 0x1B, 4, false);
        if (s < 0) break;
        statusScreen(party[s]);
        statusShown = true;
        full = false;
        break;
      }
      case 4:
        sortMenu();
        break;
      case 5: {
        SysRes sr = systemMenu();
        if (sr == SysRes::Quit) return MM::Quit;
        if (sr == SysRes::Loaded) return MM::Reload;
        break;
      }
    }
  }
}

void Game::partyWindow() {
  window(0x16, 3, 0x4B, 8);
  drawStr(0x34, 3, " HP/ MAX ");
  drawStr(0x3F, 3, " MP/ MAX ");
  for (int s = 0; s < 4; ++s) {
    if (!validSlot(s)) continue;
    const int c = party[s], x = 0x1D, row = 4 + s;
    drawStr(x, row, heroName(c));
    drawStr(x + 0x12, row, fmt("L%-2ld", heroes[c].level));
    drawStr(x + 0x16, row, fmt2("%4ld/%4ld", heroes[c].hp, calcMaxHp(c)));
    drawStr(x + 0x21, row, fmt2("%4ld/%4ld", heroes[c].mp, calcMaxMp(c)));
    const std::string st = statusStr(c);
    if (!st.empty()) drawStr(x + 0x2A, row, st);
  }
}

// ---------------------------------------------------------------- items (§9.3)
int Game::itemSelect() {
  window(4, 2, 0x1F, 0xD);
  int top = (invCursor_ / 10) * 10, pos = invCursor_ % 10;
  for (;;) {
    drawStr(0xC, 3, S("← Page ") + std::to_string(top / 10 + 1) + S(" →"));
    std::vector<std::string> items;
    for (int i = 0; i < 10; ++i) {
      const int sl = top + i;
      if (invId[sl] >= 0x80)
        items.push_back(S(kBlank11));
      else
        items.push_back(core::rawName(db.items[invId[sl]].name, 16) +
                        (invCnt[sl] == 1 ? std::string(" ") : S(" ×") + fmt("%-2ld", invCnt[sl])));
    }
    MenuRes r = menu(items, 0x16, 1, 10, 6, 4, pos, true);
    if (r.kind == MenuRes::Cancel) return -1;
    if (r.kind == MenuRes::Next) {
      top = (top + 10) % 50;
      pos = r.pos % 10;
      continue;
    }
    if (r.kind == MenuRes::Prev) {
      top = (top + 40) % 50;
      pos = r.pos;
      continue;
    }
    const int sl = top + r.pos;
    if (invId[sl] >= 0x80) {
      pos = r.pos;
      continue;
    }
    invCursor_ = sl;
    return sl;
  }
}

bool Game::itemMenu() {
  for (;;) {
    prompt(0x32, 1, 0x4D, 3, S("アイテムの選択"));
    const int sl = itemSelect();
    if (sl < 0) return false;
    const int id = invId[sl];
    window(0x16, 3, 0x21, 6);
    MenuRes r = menu({S("使う　"), S("捨てる")}, 6, 1, 2, 0x18, 4, 0, true);
    if (r.kind == MenuRes::Cancel) continue;
    if (r.kind == MenuRes::Selected && r.pos == 0) {
      if (useItemField(sl)) return true;
      continue;
    }
    // discard (including Next/Prev results, as in the original)
    prompt(0x1E, 3, 0x39, 5, S("捨てていいですか？"));
    window(0x22, 5, 0x2E, 8);
    MenuRes y = menu({S("はい　"), S("いいえ")}, 6, 1, 2, 0x24, 6, 1, false);
    if (y.kind != MenuRes::Selected || y.pos != 0) continue;
    window(0x10, 0xA, 0x3F, 0xE);
    msgSetup(0xB, 4);
    msgClear();
    msgPut(itemName(id));
    msgPut(S("を捨てました。"));
    waitRelease();
    waitPress();
    removeItem(id, true);
    mY0_ = 1;
  }
}

bool Game::useItemField(int sl) {
  const int id = invId[sl];
  const ItemDef& it = db.items[id];
  if (it.kind == 11) return castField(-1, it.value & 0x1F, sl);
  int target = 0;
  if (it.kind >= 7 && it.kind <= 10) {
    partyWindow();
    prompt(0x32, 1, 0x4D, 3, S("だれに使いますか？"));
    target = heroSelect(0, 0x1B, 4, false);
    if (target < 0) return false;
  }
  window(0x10, 0xA, 0x3F, 0xE);
  msgSetup(0xB, 4);
  msgClear();
  msgPut(itemName(id));
  msgPut(S("を使った！#"));
  const int amount = variance(it.value);
  bool ok = false;
  const bool valid = validSlot(target);
  const int c = valid ? party[target] : 0;
  switch (it.kind) {
    case 7:
      if (valid && heroes[c].hp > 0) {
        heroes[c].hp = std::min(heroes[c].hp + amount, calcMaxHp(c));
        partyWindow();
        msgPut(heroName(c));
        msgPut(S("のＨＰが"));
        msgNum(amount);
        msgPut(S(" p.回復した！_"));
        removeItem(id, false);
        ok = true;
      }
      break;
    case 8:
      if (valid && heroes[c].hp > 0) {
        heroes[c].mp = std::min(heroes[c].mp + amount, calcMaxMp(c));
        partyWindow();
        msgPut(heroName(c));
        msgPut(S("のＭＰが"));
        msgNum(amount);
        msgPut(S(" p.回復した！_"));
        removeItem(id, false);
        ok = true;
      }
      break;
    case 9:
      if (valid && heroes[c].hp > 0 && (heroes[c].status & 1)) {
        heroes[c].status &= ~1;
        partyWindow();
        msgPut(heroName(c));
        msgPut(S("の毒が消えた！_"));
        removeItem(id, false);
        ok = true;
      }
      break;
    case 10:
      if (valid && heroes[c].hp > 0 && (heroes[c].status & 2)) {
        heroes[c].status &= ~2;
        partyWindow();
        msgPut(heroName(c));
        msgPut(S("の麻痺が回復した！_"));
        removeItem(id, false);
        ok = true;
      }
      break;
    default:
      break;
  }
  if (!ok) msgPut(S("しかし、何も起きなかった！_"));
  mY0_ = 1;
  return false;
}

// ---------------------------------------------------------------- field magic (§9.6.1)
bool Game::castField(int caster, int m, int itemSlot) {
  int target = 0;
  if (m == 0 || m == 1 || m == 2 || m == 6 || m == 7) {
    partyWindow();
    prompt(0x32, 1, 0x4D, 3, S("だれに使いますか？"));
    target = heroSelect(0, 0x1B, 4, false);
    if (target < 0) return false;
  }
  window(0x10, 0xA, 0x3F, 0xE);
  msgSetup(0xB, 4);
  msgClear();
  const MagicDef& md = db.magics[m];
  if (caster >= 0) {
    const int c = party[caster];
    if (heroes[c].mp - md.mp < 0) {
      partyWindow();
      msgPut(S("ＭＰが足りない！_"));
      mY0_ = 1;
      return false;
    }
    heroes[c].mp = std::min(heroes[c].mp - md.mp, calcMaxMp(c));
    msgPut(heroName(c));
    msgPut(S("は#"));
    msgPut(nameOf(md.name));
    msgPut(S("の魔法を唱えた！#"));
    se(m <= 7 ? 13 : 20);
    for (int d = 1; d <= 12; ++d) {
      applyPalRaw(db.palet, d % 12);
      waitMs(30);
    }
  } else {
    const int id = invId[itemSlot];
    msgPut(itemName(id));
    msgPut(S("を使った！#"));
    if (m <= 7) removeItem(id, false);
  }
  const int amount = variance(md.power);
  bool ok = false, warped = false;
  const bool valid = validSlot(target);
  const int c = valid ? party[target] : 0;
  auto healHp = [&](int slot) {
    if (!validSlot(slot)) return false;
    Hero& h = heroes[party[slot]];
    if (h.hp == 0) return false;
    h.hp = std::min(h.hp + amount, calcMaxHp(party[slot]));
    return true;
  };
  switch (m) {
    case 0:
    case 1:
    case 2:
      if (healHp(target)) {
        partyWindow();
        msgPut(heroName(c));
        msgPut(S("のＨＰが"));
        msgNum(amount);
        msgPut(S(" p.回復した！_"));
        ok = true;
      }
      break;
    case 3:
    case 4:
      for (int s = 0; s < 4; ++s) healHp(s);
      partyWindow();
      msgPut(S("ＨＰがそれぞれ"));
      msgNum(amount);
      msgPut(S(" p.ずつ回復した！_"));
      ok = true;
      break;
    case 5:
      fade(1, 15);
      curScreen = warpScreen & 1023;
      reloadBlock();
      scr_.frameBorder = true;
      redrawScreen();
      objs_[0].x = warpPos % 20;
      objs_[0].y = warpPos / 20;
      palMode = warpPal;
      fieldBgm = warpBgm;
      bgmChange(fieldBgm);
      fade(14, 0);
      warped = true;
      ok = true;
      break;
    case 6:
      if (valid && heroes[c].hp == 0) {
        heroes[c].hp = std::max(1, heroes[c].recMaxHp / 8);
        partyWindow();
        msgPut(heroName(c));
        msgPut(S("が生き返った！_"));
        ok = true;
      }
      break;
    case 7:
      if (valid && heroes[c].status != 0) {
        const int st = heroes[c].status;
        heroes[c].status = 0;
        partyWindow();
        if (st & 1) {
          msgPut(heroName(c));
          msgPut(S("の毒が消えた！#"));
        }
        if (st & 2) {
          msgPut(heroName(c));
          msgPut(S("の麻痺が回復した！#"));
        }
        msgPut("_");
        ok = true;
      }
      break;
    default:
      break;
  }
  if (!ok) {
    partyWindow();
    msgPut(S("しかし、何も起きなかった！_"));
  }
  mY0_ = 1;
  return warped;
}

// ---------------------------------------------------------------- magic (§9.5)
int Game::magicSelect(int c) {
  std::vector<int> list;
  for (int m = 0; m < 32; ++m)
    if (db.heroes[c].magicLv[m] <= heroes[c].level) list.push_back(m);
  if (list.empty()) {
    window(0x1E, 7, 0x32, 9);
    drawStr(0x21, 8, S("魔法は使えません"));
    waitRelease();
    waitAnyKey();  // not a "キー押し待ち" (key wait): ignores the fast key (spec 11 §9.5.1 / 12 §6.4, v1.1)
    return -1;
  }
  window(4, 2, 0x1F, 0xC);
  int top = 0, pos = 0;
  for (;;) {
    drawStr(0xC, 3, S("← Page ") + std::to_string(top / 8 + 1) + S(" →"));
    std::vector<std::string> items;
    for (int i = 0; i < 8; ++i) {
      const int k = top + i;
      if (k < int(list.size()))
        items.push_back(core::rawName(db.magics[list[k]].name, 16) + fmt(" %2ldp.", db.magics[list[k]].mp));
      else
        items.push_back(S(kBlank11));
    }
    MenuRes r = menu(items, 0x16, 1, 8, 6, 4, pos, true);
    if (r.kind == MenuRes::Cancel) return -1;
    if (r.kind == MenuRes::Next) {
      top = (top + 8) % 32;
      pos = r.pos % 8;
      continue;
    }
    if (r.kind == MenuRes::Prev) {
      top = (top + 24) % 32;
      pos = r.pos;
      continue;
    }
    pos = r.pos;
    const int k = top + r.pos;
    if (k >= int(list.size())) continue;
    return list[k];
  }
}

bool Game::magicMenu() {
  int mem = 0;
  for (;;) {
    prompt(0x32, 1, 0x4D, 3, S("だれが使いますか？"));
    const int s = heroSelect(mem, 0x1B, 4, false);
    if (s < 0) return false;
    mem = s;
    const int c = party[s];
    if (heroes[c].hp == 0 || (heroes[c].status & 2)) continue;
    prompt(0x32, 1, 0x4D, 3, S("魔法の選択"));
    const int m = magicSelect(c);
    if (m < 0) {
      partyWindow();
      continue;
    }
    if (castField(s, m, -1)) return true;
  }
}

// ---------------------------------------------------------------- equipment (§9.4)
int Game::equipHeroList(const bool* can, int init) {
  const int x = 0x28, y = 2;
  for (int s = 0; s < 4; ++s) {
    if (!validSlot(s)) continue;
    const int c = party[s];
    drawStr(x + 2, y + s, heroName(c));
    if (can[s]) {
      drawStr(x + 0x14, y + s, fmt2("%4ld/%4ld", heroes[c].hp, heroes[c].recMaxHp) + fmt(" %4ld", heroes[c].mp));
      const std::string st = statusStr(c);
      if (!st.empty()) drawStr(x + 0x1E, y + s, st);
    } else {
      drawStr(x + 0x14, y + s, S("装備できません"));
    }
  }
  int cur = init;
  for (;;) {
    const int s = heroSelect(cur, x, y, true);
    if (s < 0) return -1;
    cur = s;
    if (can[s]) return s;
  }
}

void Game::equipMenu() {
  static const char* labels[5] = {"武器／", "　鎧／", "　盾／", "　兜／", "指輪／"};
  for (;;) {
    window(0xA, 2, 0x1A, 5);
    MenuRes r = menu({S("装備する　　"), S("装備をはずす")}, 0xC, 1, 2, 0xC, 3, 0, false);
    if (r.kind == MenuRes::Cancel) return;
    auto full = [&]() {
      window(0x10, 0xA, 0x3F, 0xE);
      msgSetup(0xB, 3);
      msgClear();
      msgPut(S("持物がいっぱいです！_"));
      mY0_ = 1;
      mH_ = 4;
    };
    if (r.pos == 0) {
      prompt(0x32, 1, 0x4D, 3, S("装備品の選択"));
      for (;;) {
        const int sl = itemSelect();
        if (sl < 0) break;
        const int id = invId[sl];
        const ItemDef& it = db.items[id];
        if (it.kind < 1 || it.kind > 5) continue;
        bool can[4];
        for (int s = 0; s < 4; ++s) can[s] = validSlot(s) && (it.equipMask & (1 << party[s]));
        window(0x1E, 6, 0x4D, 0xE);
        window(0x26, 1, 0x4D, 6);
        const int s = equipHeroList(can, 0);
        if (s < 0) continue;
        const int c = party[s];
        const int slot = it.kind - 1;
        if (heroes[c].equip[slot] < 0x80) {
          if (!addItem(heroes[c].equip[slot])) {
            full();
            return;
          }
        }
        removeItem(id, false);
        heroes[c].equip[slot] = uint8_t(id);
        window(0x1E, 5, 0x4D, 0xE);
        drawStr(0x21, 6, heroName(c));
        abilityBlock(c, 0x23, 8);
      }
    } else {
      int mem = 0;
      for (;;) {
        bool can[4] = {true, true, true, true};
        window(0x1E, 6, 0x4D, 0xF);
        window(0x26, 1, 0x4D, 6);
        const int s = equipHeroList(can, mem);
        if (s < 0) break;
        mem = s;
        const int c = party[s];
        int smem = 0;
        for (;;) {
          std::vector<std::string> items;
          for (int k = 0; k < 5; ++k)
            items.push_back(S(labels[k]) + (heroes[c].equip[k] < 0x80
                                                ? core::rawName(db.items[heroes[c].equip[k]].name, 16)
                                                : std::string(16, ' ')));
          window(0x28, 8, 0x43, 0xE);
          MenuRes m = menu(items, 0x16, 1, 5, 0x2A, 9, smem, false);
          if (m.kind == MenuRes::Cancel) break;
          smem = m.pos;
          if (!addItem(heroes[c].equip[m.pos])) {
            full();
            return;
          }
          heroes[c].equip[m.pos] = kNone;
        }
      }
    }
  }
}

// ---------------------------------------------------------------- status (§9.7)
void Game::abilityBlock(int c, int x, int y) {
  const std::string dots = S("…");
  drawStrFull(x, y, S("攻撃力") + dots + fmt("%-4ld", calcAtk(c)));
  drawStrFull(x, y + 1, S("防御力") + dots + fmt("%-4ld", calcDef(c)));
  drawStrFull(x + 4, y + 2, S("力") + dots + fmt("%-4ld", calcStr(c)));
  drawStrFull(x + 2, y + 3, S("体力") + dots + fmt("%-4ld", calcVit(c)));
  drawStrFull(x + 2, y + 4, S("知力") + dots + fmt("%-4ld", calcInt(c)));
  drawStrFull(x - 2, y + 5, S("すばやさ") + dots + fmt("%-4ld", calcAgi(c)));
  drawStrFull(x + 4, y + 6, S("運") + dots + fmt("%-4ld", calcLuck(c)));
  static const char* labels[5] = {"武器／", "　鎧／", "　盾／", "　兜／", "指輪／"};
  for (int i = 0; i < 5; ++i) {
    std::string s = S(labels[i]);
    if (heroes[c].equip[i] < 0x80) s += itemName(heroes[c].equip[i]);
    drawStr(x + 0x12, y + i, s);
  }
}

void Game::statusScreen(int c) {
  window(0x1E, 0, 0x4F, 0xF);
  drawStr(0x23, 1, heroName(c));
  drawStrFull(0x3F, 1, S("レベル") + fmt("%2ld", heroes[c].level));
  const std::string st = statusStr(c);
  if (!st.empty()) drawStr(0x35, 1, st);
  drawStrFull(0x29, 2, fmt("HP %4ld/", heroes[c].hp));
  drawStrFull(0x39, 2, fmt("max %4ld", calcMaxHp(c)));
  drawStrFull(0x29, 3, fmt("MP %4ld/", heroes[c].mp));
  drawStrFull(0x39, 3, fmt("max %4ld", calcMaxMp(c)));
  drawStr(0x23, 5, S("経験値"));
  drawStrFull(0x39, 5, fmt("%8ldp.", heroes[c].exp));
  drawStr(0x23, 6, S("次のレベルまで　あと"));
  if (heroes[c].level < 99) drawStrFull(0x39, 6, fmt("%8ldp.", expNeeded(heroes[c].level) - heroes[c].exp));
  abilityBlock(c, 0x23, 8);
  goldWindow();
}

// ---------------------------------------------------------------- sort (§9.8)
void Game::sortMenu() {
  int n = 0;
  for (int s = 0; s < 4; ++s)
    if (validSlot(s)) ++n;
  int chosen[4] = {-1, -1, -1, -1};
  int mem = 0;
  static const char* bodies[4] = {"先頭の主人公キャラを", "２番目の主人公キャラを", "３番目の主人公キャラを",
                                  "４番目の主人公キャラを"};
  int i = 0;
  while (i < n) {
    prompt(0x32, 1, 0x4D, 3, S(bodies[i]), S("選択してください"));
    const int s = heroSelect(mem, 0x1B, 4, false);
    if (s < 0) {
      if (i == 0) return;
      --i;
      drawStr(0x19, chosen[i] + 4, S("　"));
      mem = chosen[i];
      chosen[i] = -1;
      continue;
    }
    mem = s;
    bool dup = false;
    for (int k = 0; k < i; ++k)
      if (chosen[k] == s) dup = true;
    if (dup) continue;
    drawStrFull(0x19, s + 4, std::to_string(i + 1));
    chosen[i] = s;
    ++i;
  }
  uint8_t np[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  for (int k = 0; k < n; ++k) np[k] = party[chosen[k]];
  std::memcpy(party, np, 4);
  heroChr_ = heroGraphicForParty();
}

// ---------------------------------------------------------------- system (§9.9)
Game::SysRes Game::systemMenu() {
  int mem = 0, slotMem = 0;
  for (;;) {
    const bool dbg = in().debug;
    std::vector<std::string> items = {S("ゲームを保存する"), S("ゲームを読み込む"), S("終了する　　　　")};
    if (dbg) items.push_back(S("ゲーム進行モニタ"));
    window(8, 5, 0x1D, 9);
    MenuRes r = menu(items, 0x10, 1, int(items.size()), 0xA, 6, mem, false);
    if (r.kind == MenuRes::Cancel) return SysRes::Back;
    mem = r.pos;
    if (r.pos == 2) return SysRes::Quit;
    if (r.pos == 3) {
      switchMonitor();
      return SysRes::Back;
    }
    for (;;) {
      window(0xE, 6, 0x1F, 0xB);
      MenuRes s = menu({S("ＤＡＴＡ１"), S("ＤＡＴＡ２"), S("ＤＡＴＡ３"), S("ＤＡＴＡ４")}, 10, 1, 4, 0x10, 7,
                       slotMem, false);
      if (s.kind == MenuRes::Cancel) {
        slotMem = 0;
        break;
      }
      slotMem = s.pos;
      if (r.pos == 0) {
        heroX = objs_[0].x;
        heroY = objs_[0].y;
        saveSlot(s.pos);
        break;
      }
      if (loadSlot(s.pos)) {
        afterLoadRebuild();
        return SysRes::Loaded;
      }
    }
  }
}

void Game::afterLoadRebuild() {
  fade(1, 15);
  objs_[0].x = heroX;
  objs_[0].y = heroY;
  reloadBlock();
  scr_.frameBorder = true;
  redrawScreen();
  heroChr_ = vehicle == 1 ? shipGfx : vehicle == 2 ? airGfx : heroGraphicForParty();
  objs_[0].dir = 2;
  objs_[0].stage = 0;
  bgmChange(fieldBgm);
  fade(14, 0);
}

void Game::switchMonitor() {
  window(0x10, 0xA, 0x3F, 0xE);
  msgSetup(0xB, 4);
  msgClear();
  msgPut(S("★オンになっているゲーム進行スイッチ‥‥#"));
  for (int s = 0; s < 200; ++s)
    if (getSwitch(s)) msgPut(fmt("[%3ld", s) + S("番] "));
  waitRelease();
  waitPress();
  mY0_ = 1;
}

}  // namespace d1
