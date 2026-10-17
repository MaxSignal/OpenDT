# D2 エンジンの構成（後続の実装担当向け）

対象: `src/d2/`。仕様は 20〜24 章。ここでは既に実装済みの土台と、22（イベント）・23（メニュー）・
24（戦闘）の担当が埋める差し込み口（フック）を説明する。

## ファイル

| ファイル | 内容 | 担当 |
|---|---|---|
| `data.h/.cpp` | 20 章の全データ形式（packed 構造体でファイルとバイト一致）、ビルド判別、各種ローダ | 済 |
| `game.h` | `d2::Game`（全状態と全ヘルパの宣言） | 済（追加は最小限に） |
| `game.cpp` | VSYNC・待ち・入力（キーバッファ/リピート/方向読み取り/キー待ち）、パレット（トーン・フェード・色調・フラッシュ・揺れ）、ウィンドウ枠・文字・選択カーソル・メッセージウィンドウ・文章 1 行・所持金ウィンドウ、共通ゲームルール（23 §1） | 済 |
| `field.cpp` | 21 章: マップ読込/進入、描画、主人公移動、NPC、移動命令、tick、乗り物、エンカウント、戦闘呼び出し（21 §10.2）、イベント起動判定と開始/終了処理、メインループ | 済 |
| `flow.cpp` | 起動、タイトル、ニューゲーム、セーブ/ロード、デバッグ開始オプション、`createEngine` | 済 |
| `hooks.h` | 差し込み口の宣言（下記） | 済 |
| `events.cpp` | **仮実装**。22 章の担当が置き換える | 22 担当 |
| `menus.cpp` | **仮実装**。23 章の担当が置き換える | 23 担当 |
| `battle.cpp` | **仮実装**。24 章の担当が置き換える | 24 担当 |

担当ごとにファイルを増やしてよい（`events_*.cpp` など。CMake は `src/d2/*.cpp` を自動で拾う）。
担当固有の状態は、自分のファイル内の構造体か `Game` への最小限の追加で持つ（同時編集を避けるため、
`game.h` への追加は末尾の「担当別」節にまとめること）。

## 差し込み口（`hooks.h`）

```cpp
int  runEvent(Game& g, int eventNo, int trigger);                 // 22: 受理後に呼ばれる。§4.1→§5→§4.2
void fieldMenu(Game& g);                                          // 23 §3
void shopBuySell(Game& g, const std::vector<uint8_t>& goods);     // 23 §11.1（0x3C）
void shopBuy(Game& g, const std::vector<uint8_t>& goods);         // 23 §11.2（0x3D）
void shopSell(Game& g);                                           // 23 §11.3（0x3E）
void inn(Game& g, int price);                                     // 23 §12（0x3F）
int  battle(Game& g, int group, int gameOverMode);                // 24（遷移演出と §4.5 の後始末を含む）
```

- `runEvent` の前に、21 §7.3 / 22 §3.2 の受理判定は `Game::tryStartEvent` が済ませている。
  `runEvent` の中で `g.beginEvent(i, t)`（22 §4.1 の 1・3〜7。振り向きを含む）→ コマンドループ →
  `g.endEvent()`（22 §4.2）を呼ぶ。ラベル表・分岐結果表・作業領域は events 側で持つ。
  自分を動かすコマンド（0x4B/0x50/0x51/0x56）では `g.run_.movedSelf = true` にする。
  場所移動で別マップになった場合は `g.run_.savedBattleBgm` を新マップの戦闘 BGM 名で置き換える（22 0x10）。
  `g.run_.startMap` が開始時のマップ。
- STOP（テストプレイ時の Ctrl+取消）は `g.checkStop()` が `d2::EventAbort` を投げる（イベント実行中のみ）。
  `keyWait`・`select`・`tick` は内部で `checkStop()` を呼ぶ。`runEvent` はこれを捕まえて 22 §5.4 の手順
  （`st.eventRunning=0` → `waitHeroStill()` → `drawField()` → `endEvent()`、戻り値 0）を行う。
- 戦闘の呼び出しは、呼び出し側処理（21 §10.2: 戦闘 BGM、グラフィック再読込、消去、ページ更新、マスク、
  色調再適用、再描画、T=100、BGM 復帰）込みの `g.callBattle(group, gameOverMode, eraseEvent)` を使う。
  `battle()` 本体は 24 章の範囲だけを行う。ゲームオーバーは `throw d2::ReturnToTitle()`。
- 戦闘背景番号は呼び出し側が `g.st.battleBg` に設定する。

## 制御の流れ（例外）

| 例外 | 意味 | 捕まえる場所 |
|---|---|---|
| `d2::ReturnToTitle` | タイトルへ戻る（全滅、イベント 0x61） | `titleLoop`（状態は PLAYER.DAT から戻る） |
| `d2::QuitProgram` | プログラム終了（システムメニュー「終了」、イベント 0x62）。先にブラックアウト等は呼び出し側で | `run`（`--no-quit` ならタイトルへ） |
| `d2::EventAbort` | STOP によるイベント中断 | `runEvent` |
| `core::QuitGame` | ホストからの終了要求 | ホスト |

## 主な状態（`Game` のメンバ）

- `members[12]`（`Member`、PLAYER.DAT 200 バイト記録そのもの）、`st`（`StateBlock`、0x2A8 バイト）、
  `inventory[260][2]`、`switches[1001]`、`vars[100]`。セーブは `buildSave()` / `saveSlot(k)` / `loadSlot(k)`。
- `db`（`Database`）: アイテム・魔法・敵・敵グループ・WORD.DAT（`db.wordAt(off)`）・マップ名・外字・タイトル画像。
- `map`（`MapData`）: ヘッダ、160×160 グリッド、区域表、版数、イベント（`map.events[i]` は
  イベント領域内の `EventRec*`、無ければ nullptr）。スクリプトは `map.script(i, page, &len)`（len は終端込み）。
- `chips`（属性・エンカウント種別・戦闘背景）、`chipset` / `chrSlot[4]` / `cache[33]`（パターン）。
- `upperMap` / `lowerMap`（イベント位置図）、`viewX/viewY`（ビュー原点）。
- パレット: `curPal`（8 ビット）、`basePal`（チップセット 4 ビット）、`tone`。表示反映は `applyPalette()`。
- `frameCounter`（自走カウンタ）、`waitCounter`（待ちカウンタ）、`embed`（`\@` の埋め込み文字列）。
- `build`（`Build::B9606/B9608/B9703/B9710`、`isOldBuild(build)`）、`testPlay`（`--no-title`）。

## 主なヘルパ

| 用途 | 関数 |
|---|---|
| 時間 | `vsync()`、`waitFrames(n)`、`waitMs(ms)`（56.42 Hz 換算・繰り越し）、`waitUnits(n)`（21 §1.2） |
| 音 | `sfx(n)`（ドライバには n+10）、`playBgm(name9)`、`stopBgm()`、`bgmEqual`（9 バイト）/`bgmEqual8` |
| 入力 | `popKey()`（`kKeyEsc` 等、無ければ `kKeyNone`）、`clearKeys()`、`recordLatch()`、`readDirection()`、`keyWait()`（キー離し→押し、乱数消費）、`keyWaitMark()`（▼つき）、`cur`/`prev`（今回/前回の入力） |
| パレット | `setTone(t)`、`vsyncTone(t)`、`blackOut/blackIn/whiteOut/whiteIn(s)`、`toneChange(k, s)`、`reapplyTone()`、`flash()`、`shake()`、`setPal4(i,r,g,b)` |
| 描画 | `drawText(x,y,sjis,color)`（D2 グラフィック文字、太字・透明）、`textWidth()`、`drawWindow(x,y,w,h)`（23 §2.1）、`drawCursor()`、`select(x,y,n,init,mode)`（22 §6.4）、`fieldMask()/menuMask()/windowMask()/clearTextLayer()`、`goldWindow(x,y)`、`scr`（`core::Screen`） |
| メッセージ | `openMessage()`、`closeMessage()`、`messageLine(line, s)`（22 §6.2 の制御記法つき 1 行。`\^` を見たら true） |
| フィールド | `drawField()`、`enterMap()`、`updatePages()`、`activePage(i)`、`eraseEvent(i, permanent)`、`tick(n)`、`waitHeroStill()`、`waitEventStill(i)`、`clearMoveCommands()`、`setHeroMoveCommand(m)`、`setEventMoveCommand(i,m)`、`reloadHeroSprite()`、`reloadVehicleSprites()`、`reloadAllSprites()`、`boardShip(withBgm)`、`callBattle(...)`、`chipAt/attrAt(x,y)` |
| ルール | `recalc(m)`、`recalcAll()`、`levelUp(m)`、`levelDown(m)`、`addHp(m,n)`、`fullHeal(m)`、`gameOver()`、`joinParty/leaveParty/inParty/leader()`、`learnMagic/forgetMagic`、`addItem/removeItem/packInventory/moveItemToEnd/discardItem/hasItem/itemCount`、`addGold/subGold`、`updatePoison()`、`expForLevel/statForLevel` |
| フロー | `slotSelect(x,y)`、`saveSlot(k)`、`loadSlot(k)`（20 §9.3 のブラックアウト込み。ファイルが無ければ false） |

文字列定数（「ニューゲーム」等の RPG.EXE 内蔵文字列）は UTF-8 で書いて `core::utf8ToSjis()` で SJIS にする。

## 仮実装の現状

- `events.cpp`: 0x00/0x0B 終了、0x01(+0x65) 文章、0x02/0x03 スイッチ、0x0A 消去、0x10 場所移動、0x13 ウェイト、
  0x1C/0x1D、0x34/0x35、0x46/0x47/0x48、0x52/0x53 のみ。分岐は解釈せず全部順に実行する。
  自動イベントの無限ループ対策として最後に 1 VSYNC 待つ（本実装では不要なので消すこと）。
- `menus.cpp`: メニューを開いてすぐ閉じる。店・宿は何もしない。
- `battle.cpp`: 24 §4.5 の後始末だけをして勝利を返す。

## テスト

- `tests/test_d2.cpp`（20・21 章の受け入れ観点）。
- 参考画面との比較: `-k "60 shot:01_title 30 z 30 z 150 shot:02_start z 70 z 70 shot:03_after_ok x 60 shot:04_menu x 60 r*40 l*40 shot:05_walk" -n 1500`
  で全 23 本を撮り、`reference_shots/<game>/` と画素比較する。現時点で 01_title は全一致、
  dino・keep・knuckle は 5 枚とも一致。残りの差はイベントコマンド（選択肢・ピクチャ・色調など）の未実装による。
