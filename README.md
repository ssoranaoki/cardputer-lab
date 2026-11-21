# cardputer-lab

M5Stack Cardputer Adv（ESP32-S3）で、OS のしくみを手を動かして学ぶためのプログラム集です。

「コアは1つなのに、なぜ2つの仕事が同時に動いて見えるのか？」という問いから始め、FreeRTOS のタスクを観察し、自分で順番係を作りました。小さなシェル、ファイル、メモ帳、ホーム画面、日本語入力へと少しずつ広げています。

## プログラム

プログラムは1つずつ `src/<名前>/` に置き、PlatformIO の環境（`[env:<名前>]`）で切り替えます。

| 環境 | 内容 |
|---|---|
| `hello` | 書き込みの確認。画面に文字を出し、押したキーを表示する |
| `demo1` | UNO 流。2つの仕事を `loop()` の中で `millis()` を見ながら順番に回す |
| `demo2` | OS 流。同じ2つの仕事を FreeRTOS のタスクにして、順番は OS に任せる |
| `demo3` | キーで片方のタスクを止める・また動かす（`vTaskSuspend` / `vTaskResume`） |
| `demo4` | 交代の帯。コア1でいま動いているタスクを 1ms ごとに記録し、色の帯で見せる |
| `junban1` | 順番係を自分で作る（協力型）。FreeRTOS のタスクを使わずに仕事を交代させる |
| `shell1` | 小さなシェル。`ps`・`run`・`stop`・`go`・`kill`・`free` |
| `shell2` | シェルを広げる。履歴、Tab での補完、LittleFS の `ls`・`cat` |
| `shell3` | ファイルを書く。`echo`（`>`・`>>`）・`rm`・`mkdir`・`cd`・`pwd` |
| `edit1` | ホーム画面、メモ帳、ファイラー、前の画面から再開、日本語入力（SKK 方式）。既定の環境 |

## 必要なもの

- M5Stack Cardputer Adv
- データ通信ができる USB ケーブル（充電専用のケーブルでは書き込めません）
- PlatformIO（VSCode の拡張機能、または CLI）
- Python（`edit1` の辞書を作るときに使います）

## ビルドと書き込み

```sh
# edit1 だけ、先に辞書を作る（下の「日本語入力の辞書」を参照）
python tools/make_skkdict.py

# 書き込み（-e で環境を選ぶ）
pio run -e demo2 -t upload

# シリアルの出力を見る（書き込みが終わってから、別に動かす）
pio device monitor
```

VSCode の場合は、下のステータスバーで環境を選んで Upload を押します。シリアルの出力はターミナルで `pio device monitor` を使って見てください。VSCode のシリアルモニター画面で監視を終了すると、ボードが書き込みモードで起動し直すことがあります。

### 書き込みが「No serial data received」で失敗するとき

`espressif32@6.7.0` に付いてくる esptool 4.5.1 には既知の問題があります（espressif/esptool #868・#1070）。`platformio.ini` では esptool 4.9.0 を使うように指定しています。それでも失敗する場合は、`platformio.ini` のコメントにある手順で、足りない部品（intelhex・PyYAML）を入れてください。

手で書き込みモードにするには、電源を OFF にし、G0 ボタンを押したまま USB を差してから離します。

## 日本語入力の辞書

`edit1` の漢字変換は SKK 辞書 [SKK-JISYO.M](https://github.com/skk-dev/dict) をプログラムに埋め込んで使います。
辞書のライセンスは GPL v2 以降なので、このリポジトリには入れていません。
`python tools/make_skkdict.py` を実行すると、辞書を GitHub から `third_party/skk/` に取ってきて、`src/edit1/skkdict.h` を作ります。

`edit1` での入力のしかたは次のとおりです。

- ローマ字で打つと、ひらがなになります
- 大文字で始めると ▽（読み）になり、Space で漢字の候補を選びます
- ▽ の中で大文字を打つと、そこから送りがなになります（例: `KaU` → 買う）

## 補助スクリプト（Windows）

- `scripts/serial-read.py`: シリアルの出力を決めた秒数だけ読み、`logs/` に保存します。PlatformIO 付属の Python で動かします
- `scripts/check-boot-mode.bat`: ボードをリセットせずに、書き込みモードかどうかを確かめます

## ライセンス

コードは [MIT License](LICENSE) です。

SKK 辞書（`third_party/skk/SKK-JISYO.M`）と、辞書から作る `src/edit1/skkdict.h` は GPL v2 以降で、このリポジトリには含みません。辞書を入れてビルドしたファームウェアを配るときは、GPL の条件に従ってください。詳しくは [third_party/skk/README.md](third_party/skk/README.md) を見てください。

ソースのコメントには、作るときに使った発表メモや教科書の章（`docs/presentation-*.md`、「第8章」など）への言及があります。これらはこのリポジトリには含みません。
