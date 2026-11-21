"""Cardputer のシリアル出力を「決めた秒数だけ」読んで、画面とログに出す。

pio device monitor は止めるまで動き続けるので、AI（MulmoTerminal のセルの Claude Code）が
「書き込み → 読む → 結果を見て直す」を自分で回すには使いにくい。これは N 秒で必ず終わり、
ポートを閉じるので、次の書き込み（pio run -t upload）とぶつからない。

使い方（Windows の PowerShell、projects\\cardputer-lab で）:
    & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" scripts\\serial-read.py            # 10 秒読む
    & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" scripts\\serial-read.py -s 20      # 20 秒読む
    & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" scripts\\serial-read.py --reset    # 起動し直して setup() から読む
    & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" scripts\\serial-read.py --send "ps" # 1 行送ってから読む（シェル1 など）

ログ: logs/serial-<日時>.log と logs/serial-latest.log（UTF-8）。
      ワークスペースの中なので、MulmoClaude（サンドボックス）からも読める。

PlatformIO 付属の Python（penv）には pyserial 3.5 が入っている（platformio 6.2.0 の依存 pyserial==3.5.*）。

DTR / RTS について（platformio.ini の monitor_dtr = 0 / monitor_rts = 0 と同じ考え）:
  ESP32-S3 の USB Serial/JTAG は RTS0・DTR1 で「次は書き込みモードで起動」、RTS1・DTR0 で「リセット」になる
  （ESP32-S3 技術リファレンスマニュアル 33.3.2 表 33.3-2）。開く前に両方 0 にしておき、ボードの状態を変えない。
  --reset のときだけ RTS を一瞬 1 にしてリセットする（DTR は 0 のまま＝書き込みモードにはならない）。
"""

import argparse
import datetime
import pathlib
import sys
import time

import serial
import serial.tools.list_ports

ESPRESSIF_VID = 0x303A  # ESP32-S3 の USB Serial/JTAG
PROJECT_DIR = pathlib.Path(__file__).resolve().parent.parent
LOG_DIR = PROJECT_DIR / "logs"


def find_port():
    """Espressif の USB ポートを1つ探す。見つからなければ None。"""
    ports = [p for p in serial.tools.list_ports.comports() if p.vid == ESPRESSIF_VID]
    if len(ports) == 1:
        return ports[0].device
    if len(ports) > 1:
        names = ", ".join(p.device for p in ports)
        print(f"[serial-read] Espressif のポートが複数あります: {names}。-p で選んでください", file=sys.stderr)
    return None


def open_port(port, baud):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dtr = False  # 開く前に決めておくと、開いた瞬間から 0 のまま
    ser.rts = False
    ser.open()
    return ser


def main():
    parser = argparse.ArgumentParser(description="Cardputer のシリアル出力を N 秒読む")
    parser.add_argument("-p", "--port", help="COM ポート（省略すると Espressif のポートを探す）")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="速さ（platformio.ini の monitor_speed と同じ 115200）")
    parser.add_argument("-s", "--seconds", type=float, default=10.0, help="読む秒数（既定 10）")
    parser.add_argument("--reset", action="store_true", help="読み始める前にボードを起動し直す（setup() の出力も見たいとき）")
    parser.add_argument("--send", help="開いてすぐ送る 1 行（末尾に \\r\\n を付ける）")
    args = parser.parse_args()

    # Windows の端末の文字コードで落ちないようにする（表示できない字は ? になる。ログは UTF-8 のまま）
    sys.stdout.reconfigure(errors="replace")

    port = args.port or find_port()
    if not port:
        print("[serial-read] ボードが見つかりません。USB（データ通信できるケーブル）を確かめてください。"
              " `pio device list` で COM 番号を見て -p COM4 のように渡すこともできます", file=sys.stderr)
        return 2

    LOG_DIR.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = LOG_DIR / f"serial-{stamp}.log"

    deadline = time.monotonic() + args.seconds
    ser = None
    chunks = []
    reopened = 0

    print(f"[serial-read] {port} を {args.seconds:g} 秒読みます（{args.baud} bps）")
    try:
        ser = open_port(port, args.baud)
        if args.reset:
            # esptool v4.9.0 reset.py の HardReset(uses_usb=True) と同じ手順。
            # Windows の usbser.sys は RTS だけ変えても届かないので、同じ値の DTR を書いて送らせる（_setRTS の回避策）
            ser.rts = True
            ser.dtr = ser.dtr
            time.sleep(0.2)
            ser.rts = False
            ser.dtr = ser.dtr
            time.sleep(0.2)
        if args.send is not None:
            ser.write((args.send + "\r\n").encode("utf-8"))

        while time.monotonic() < deadline:
            try:
                data = ser.read(4096)
            except serial.SerialException:
                # リセット直後は Windows から一度ポートが消えることがある → 期限まで開き直す
                try:
                    ser.close()
                except serial.SerialException:
                    pass
                ser = None
                while ser is None and time.monotonic() < deadline:
                    time.sleep(0.3)
                    try:
                        ser = open_port(port, args.baud)
                        reopened += 1
                    except serial.SerialException:
                        ser = None
                continue
            if data:
                text = data.decode("utf-8", errors="replace")
                chunks.append(text)
                sys.stdout.write(text)
                sys.stdout.flush()
    except serial.SerialException as e:
        print(f"\n[serial-read] ポートを開けません: {e}", file=sys.stderr)
        print("[serial-read] 別のモニター（pio device monitor・VSCode）が開いていないか確かめてください", file=sys.stderr)
        return 3
    finally:
        if ser is not None:
            ser.close()

    text = "".join(chunks)
    log_path.write_text(text, encoding="utf-8")
    (LOG_DIR / "serial-latest.log").write_text(text, encoding="utf-8")

    lines = text.count("\n")
    note = f"、開き直し {reopened} 回" if reopened else ""
    print(f"\n[serial-read] 終わり: {len(text)} 文字・{lines} 行{note}。ログ: {log_path.relative_to(PROJECT_DIR)}")
    if not text:
        print("[serial-read] 何も届きませんでした。プログラムがシリアルに書いているか、--reset で起動し直して試してください")
    return 0


if __name__ == "__main__":
    sys.exit(main())
