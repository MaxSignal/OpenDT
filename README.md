# Dante98CR

PC-9801 用「RPGツクール Dante98」（D1, 1992）と「RPGツクール Dante98 II」（D2, 1996）で作られたゲームを
現代の環境で遊ぶためのプレイヤーを、**クリーンルーム方式**で再実装したもの。原本の RPG.EXE（ゲーム実行部）と
MUSIC.COM（FM 音源ドライバ）の挙動を高水準で再現する。エミュレータではなく、ゲームのデータファイルを
直接読み込んで動かす。

## クリーンルームの方針

- 実装の根拠は、別の担当者が自然言語で書いた仕様書（`dante98_cleanroom_spec/`）と、サンプルゲームのデータ
  ファイルだけ。
- 原本の実行ファイルの逆アセンブル・逆コンパイルはしていない（仕様書が許可する版判別のためのバイト読み取り
  と、MUSIC.COM の版文字列の検索のみ）。
- インターネット上の Dante98 / MUSIC.COM / 既存再実装の情報は使っていない。
- 原本を DOSBox-X（PC-98 モード）で**動かして外から観察する**ことだけは許可を得て行った
  （`docs/ORIGINAL_OBSERVATION.md`）。
- 仕様が曖昧な点は推測で埋めず `docs/questions_*.md` に「仕様への質問」として記録し、
  仮の実装には `PROVISIONAL(Qn)` を付けた。
- 外部部品: [ymfm](third_party/ymfm)（YM2203 エミュレータ、BSD-3-Clause）、東雲ビットマップフォント
  （パブリックドメイン。`tools/gen_font.py` で字形表 `src/core/font_data.cpp` を生成）。

## ビルド

必要なもの: C++17 コンパイラ、CMake 3.16 以上、zlib、SDL2（デスクトップ版のみ）。

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

生成物:

| 実行ファイル | 内容 |
|---|---|
| `dante98cr` | デスクトップ版（SDL2）。ゲーム選択画面つき |
| `dante98cr-headless` | 画面なしの実行器。台本で入力し、PNG と WAV を出力する（テスト用） |
| `mmlplay` | MML / SOUND.DAT の再生・一括コンパイル確認ツール |
| `unit_tests` | ユニットテスト |

## 遊び方

```
build/dante98cr <ゲームのフォルダ | ゲームが入ったフォルダ> [--no-title] [--scale N] [--fullscreen] [--mute]
```

- ゲームのフォルダ（RPG.EXE のある場所）を指定すると直接起動する。それ以外のフォルダを指定すると、その下を
  探してゲーム選択画面を出す。
- `--no-title`: 原本を引数なしで起動したのと同じ。D1 はオープニングを省略、D2 はテストプレイモード
  （デバッグメニューが使える）。

| キー | 意味 |
|---|---|
| カーソル / テンキー 8 2 4 6 | 移動・カーソル |
| Z / Enter / Space | 決定・調べる |
| X / Esc / Backspace | 取消・メニュー |
| Q / Shift | メッセージ早送り（D1 の Q、D2 の SHIFT） |
| Ctrl | デバッグ（D1 の進行モニタ、D2 テストプレイの STOP は Esc+Ctrl） |
| Page Up / Page Down | PC-98 の ROLL UP / ROLL DOWN（D2 デバッグメニューの変数エディタで値 +1 / −1） |
| Tab（押している間） | 早送り |
| F12 | スクリーンショット（`dante98cr_NNN.png`） |
| Alt+Enter | 全画面切り替え |

## ヘッドレス実行器

```
build/dante98cr-headless GAMEDIR -k "60 shot:title 30 z 30 z 150 shot:start" -o OUTDIR [-w out.wav] [-n 1500] [-v]
    [--no-title] [--warp M,X,Y] [--battle G] [--gold N] [--item ID] [--seed N]
build/dante98cr-headless --games FOLDER -o OUTDIR
```

台本の語: `N`（N フレーム待つ）、`z x u d l r q c p n`（決定・取消・上下左右・高速・Ctrl・ROLL UP・ROLL DOWN を
同時押し、`*N` で N フレーム押し続ける）、`shot:名前`、`quit`。

## テスト

```
build/unit_tests                 # ユニットテスト
tools/run_all_refs.sh            # 全サンプルで参考画面と画素比較
```

## 構成

| ディレクトリ | 内容 | 仕様の章 |
|---|---|---|
| `src/core` | 画面モデル（640×400・16 色・テキスト層・スクロール）、文字、乱数、ファイル、PNG/WAV | 01 |
| `src/music` | MUSIC.COM 互換ドライバ（MML コンパイラ、シーケンサ、効果音）、ymfm 接続 | 02 |
| `src/d1` | D1 エンジン | 10, 11, 12, 19 |
| `src/d2` | D2 エンジン | 20–24 |
| `src/app` | ランチャー、SDL フロントエンド、ヘッドレス実行器、mmlplay | 01 §7, §9 |
| `tools/dosbox` | 原本観察用ハーネス（DOSBox-X、PC-98 モード） | — |
| `docs` | 開発ルール、D2 の構造、仕様への質問 | — |
