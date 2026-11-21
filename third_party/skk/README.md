# SKK 辞書（SKK-JISYO.M）

日本語入力（`edit1`）の漢字変換は SKK 辞書の SKK-JISYO.M を使います。
辞書はこのリポジトリに入っていません。`python tools/make_skkdict.py` を実行すると、次の出典からここに取ってきて、`src/edit1/skkdict.h` を作ります。

- 出典: https://github.com/skk-dev/dict （ファイル: https://raw.githubusercontent.com/skk-dev/dict/master/SKK-JISYO.M ）
- ライセンス: GNU General Public License version 2 以降（GPL v2 or later）。詳しくは SKK-JISYO.M の冒頭を見てください

辞書と、辞書から作った `skkdict.h` は GPL です。このリポジトリのコード（MIT）とはライセンスが違います。
辞書を入れてビルドしたファームウェアを配るときは、GPL の条件に従ってください。
