# Dante98CR 開発ルール

クリーンルーム方式の Dante98 / Dante98 II プレイヤー再実装。

## クリーンルーム規則（厳守）

1. 使ってよい情報: `/home/claude/work/spec/dante98_cleanroom_spec/` の仕様書一式、サンプルゲームのデータファイル
   （`/home/claude/samples/`）、DOSBox 等のエミュレータで原本を動かして観察した結果、汎用ライブラリ（ymfm、東雲フォント）。
2. 禁止: RPG.EXE・MUSIC.COM などの実行ファイルの逆アセンブル・逆コンパイル・バイナリ解析（仕様が許可する
   版判別のためのバイト読み取りと版文字列検索のみ可）。インターネット上の Dante98 / MUSIC.COM / OpenDante98 に
   関する情報の検索・取得（WebSearch/WebFetch/curl を使わない）。
3. 仕様が曖昧・矛盾するときは推測で埋めず `docs/questions_<module>.md` に「仕様への質問」として記録する。
   応急の実装を入れる場合は、コードに `// PROVISIONAL(Qn):` と書き、質問番号を対応させる。

## 構成

| ディレクトリ | 内容 | 仕様 |
|---|---|---|
| src/core | 画面モデル、文字、乱数、ファイル、PNG/WAV、Host 境界 | 01 |
| src/music | MUSIC.COM 互換ドライバ、ymfm 接続、MusicPlayer | 02, 01 §8 |
| src/d1 | D1 エンジン | 10, 11, 12, 19 |
| src/d2 | D2 エンジン | 20–24 |
| src/app | ランチャー、SDL フロントエンド、ヘッドレス実行器 | 01 §7, §9 |
| tests | ユニットテスト（`tests/test_<module>.cpp`） | 各章の受け入れテスト観点 |

## ビルド

```
cmake -S . -B build-<module> && cmake --build build-<module> -j2 --target <targets>
```

並行作業中は各担当が自分専用のビルドディレクトリ（`build-music`、`build-d1`、`build-d2`）を使う。
`src/core` と `src/app` の変更は追加のみ・最小限にし、他モジュールのビルドを壊さないこと。

## ヘッドレス実行器

```
build/dante98cr-headless GAMEDIR -k "60 shot:01_title 30 z 30 z 150 shot:02_start" -o OUTDIR [-w out.wav] [-v]
```
