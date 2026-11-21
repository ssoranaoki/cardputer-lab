// 単位0: 書き込みの確認（Hello）
// 画面に文字を出し、押したキーを表示する。シリアルにはボードの種類と、
// loop() がどのコアで動いているかを出す（デモ4の下準備）。
// 書き方は M5Cardputer 1.1.1 の examples/Basic/keyboard に合わせた（setTextFont は M5GFX で非推奨のため setFont）。

#include <M5Cardputer.h>

void showCenter(const String& text) {
    M5Cardputer.Display.clear();
    M5Cardputer.Display.drawString(text, M5Cardputer.Display.width() / 2,
                                   M5Cardputer.Display.height() / 2);
}

void setup() {
    auto cfg = M5.config();
    // M5Unified の serial_baudrate は既定 0（＝Serial.begin を呼ばない）。
    // ARDUINO_USB_MODE=1 では arduino-esp32 2.0.16 も自動では Serial.begin しないので、ここで指定する。
    cfg.serial_baudrate = 115200;
    M5Cardputer.begin(cfg, true);  // true: キーボードも使う
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.setFont(&fonts::FreeSansBold12pt7b);
    showCenter("Hello Cardputer");

    Serial.printf("board type: %d\n", (int)M5.getBoard());
    Serial.printf("setup() core: %d\n", xPortGetCoreID());
}

// シリアルの確かめ用: 1秒ごとに数を送り、画面の左下にも同じ数を出す。
// 画面の数が増えていれば新しいプログラムが入っている。モニターに出なければ、送る道が問題。
unsigned long lastTick = 0;
unsigned long tickCount = 0;

void loop() {
    M5Cardputer.update();
    if (millis() - lastTick >= 1000) {
        lastTick = millis();
        tickCount++;
        Serial.printf("tick %lu  (board type: %d)\n", tickCount, (int)M5.getBoard());
        M5Cardputer.Display.setTextDatum(bottom_left);
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.fillRect(0, M5Cardputer.Display.height() - 10, 60, 10, BLACK);
        M5Cardputer.Display.drawString(String("tick ") + tickCount, 2,
                                       M5Cardputer.Display.height() - 1);
        M5Cardputer.Display.setFont(&fonts::FreeSansBold12pt7b);
        M5Cardputer.Display.setTextDatum(middle_center);
    }
    if (M5Cardputer.Keyboard.isChange()) {
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
            String keys = "";
            for (auto c : status.word) {
                keys += c;
            }
            if (keys.length() > 0) {
                showCenter(keys);
                // setup() の出力はモニターを開く前に流れて見えないことがあるので、
                // ボードの種類はキーを押すたびにも出す
                Serial.printf("key: %s  (board type: %d, loop() core: %d)\n",
                              keys.c_str(), (int)M5.getBoard(), xPortGetCoreID());
            }
        } else {
            showCenter("Hello Cardputer");
        }
    }
}
