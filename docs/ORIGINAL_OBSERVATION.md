# 原本の挙動観察（DOSBox-X PC-98 モード）

ユーザーの許可により、原本（RPG.EXE / MUSIC.COM）をエミュレータで**実行して外から観察する**ことは可。
逆アセンブル・メモリダンプ解析・デバッガでのコード追跡は不可（ブラックボックス観察のみ）。

## 使い方

```
H98_DISPLAY=101 python3 tools/dosbox/h98.py GAMEDIR OUTDIR "MUSIC -R;RPG" "5 shot:a key:Return 3 shot:b hold:Right:1.0 shot:c"
```

- 台本の語: 秒数（実数）で待つ / `key:名前`（xdotool のキー名: Return, Escape, Up, Down, Left, Right, z, x, Shift_L …）
  / `hold:名前:秒` / `shot:名前`（OUTDIR/名前.png、640×400）。
- 並行して使う場合は `H98_DISPLAY` を担当ごとに変える（music=101, d1=102, d2=103, その他=104〜）。
- FM 音源は PC-9801-26K（I/O 0x188, IRQ 12）。`MUSIC -R` で常駐させてから `RPG` を起動する。
- D1 はオープニングでキー入力待ちになる。時間は実時間なので、フレーム単位の厳密な比較には向かない
  （画面配置・色・文言・進行の確認に使う）。
- 実時間で動くのでエミュレータ速度（cycles=max）に依存する。
