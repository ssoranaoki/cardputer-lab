// デモ1: UNO 流（millis() 版）
//
// 2つの仕事を、loop() の中で millis() を見ながら順番に回す。UNO 速習キットと同じやり方。
//   仕事A … 100ms ごとに数を1つ増やして、画面の左に出す
//   仕事B …  20ms ごとに玉を少し動かして、画面の右に出す
// [h] キーで、仕事A に「重い処理」（300ms かかる計算のまね）を入れたり外したりする。
// 重い処理が入ると、仕事B も待たされて玉がカクつく。下の段の「gap」（前回からの間隔）に数字で出る。
//
// デモ2（OS 流）では、同じ見た目をタスク2つで書いて比べる。

#include <M5Cardputer.h>

// ---- 画面の区切り（240×135。docs/presentation.md の配置図どおり） ----
constexpr int SCREEN_W = 240;
constexpr int TOP_H = 85;       // 上の段（左: 仕事A／右: 仕事B）
constexpr int LEFT_W = 110;     // 仕事A の幅
constexpr int RIGHT_W = SCREEN_W - LEFT_W;
constexpr int BOTTOM_H = 135 - TOP_H;  // 下の段（回数・間隔・キーの案内）

// 区切りごとに絵を描く紙（スプライト）。紙に描いてから一度に画面へ貼るので、ちらつかない
M5Canvas canvasA(&M5Cardputer.Display);
M5Canvas canvasB(&M5Cardputer.Display);
M5Canvas canvasInfo(&M5Cardputer.Display);

// ---- 仕事A: 数を数える ----
constexpr unsigned long A_INTERVAL_MS = 100;
unsigned long aLast = 0;   // 前回動いた時刻
unsigned long aRuns = 0;   // 動いた回数
unsigned long aGap = 0;    // 前回からの間隔（ms）

// ---- 仕事B: 玉を動かす ----
constexpr unsigned long B_INTERVAL_MS = 20;
unsigned long bLast = 0;
unsigned long bRuns = 0;
unsigned long bGap = 0;
float ballX = 20.0f;
float ballSpeed = 2.5f;    // 1回に動く距離（px）
constexpr int BALL_R = 8;

// ---- 重い処理 ----
constexpr unsigned long HEAVY_MS = 300;
bool heavy = false;

// 下の段の書き直し
constexpr unsigned long INFO_INTERVAL_MS = 200;
unsigned long infoLast = 0;

// シリアルへ送る間隔
constexpr unsigned long SERIAL_INTERVAL_MS = 1000;
unsigned long serialLast = 0;

// 計算のまね: 決まった時間、ひたすら数を足し続ける（delay と違い、その間 CPU を使い切る）
void busyWork(unsigned long ms) {
    unsigned long start = millis();
    volatile uint32_t x = 0;
    while (millis() - start < ms) {
        x++;
    }
}

void jobA() {
    aRuns++;
    if (heavy) {
        busyWork(HEAVY_MS);
    }
    canvasA.fillSprite(BLACK);
    canvasA.setFont(&fonts::Font2);
    canvasA.setTextColor(CYAN);
    canvasA.drawString("job A", 8, 6);
    canvasA.setFont(&fonts::Font4);
    canvasA.setTextColor(WHITE);
    canvasA.drawString(String(aRuns), 8, 38);
    canvasA.pushSprite(0, 0);
}

void jobB() {
    bRuns++;
    ballX += ballSpeed;
    if (ballX < BALL_R + 4) {
        ballX = BALL_R + 4;
        ballSpeed = -ballSpeed;
    } else if (ballX > RIGHT_W - BALL_R - 4) {
        ballX = RIGHT_W - BALL_R - 4;
        ballSpeed = -ballSpeed;
    }
    canvasB.fillSprite(BLACK);
    canvasB.drawFastVLine(0, 0, TOP_H, DARKGREY);  // 左右の仕切り
    canvasB.setFont(&fonts::Font2);
    canvasB.setTextColor(ORANGE);
    canvasB.drawString("job B", 8, 6);
    canvasB.fillCircle((int)ballX, 52, BALL_R, ORANGE);
    canvasB.pushSprite(LEFT_W, 0);
}

void drawInfo() {
    canvasInfo.fillSprite(BLACK);
    canvasInfo.drawFastHLine(0, 0, SCREEN_W, DARKGREY);  // 上下の仕切り
    canvasInfo.setFont(&fonts::Font2);
    canvasInfo.setTextColor(WHITE);
    canvasInfo.drawString(String("A: ") + aRuns + " runs  gap " + aGap + "ms", 4, 3);
    canvasInfo.drawString(String("B: ") + bRuns + " runs  gap " + bGap + "ms", 4, 18);
    canvasInfo.setTextColor(heavy ? RED : GREEN);
    canvasInfo.drawString(String("[h] heavy: ") + (heavy ? "ON" : "OFF") +
                              "   core " + xPortGetCoreID(),
                          4, 33);
    canvasInfo.pushSprite(0, TOP_H);
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;  // 単位0で分かったこと: 指定しないと Serial.begin されない
    M5Cardputer.begin(cfg, true);  // true: キーボードも使う
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);

    canvasA.createSprite(LEFT_W, TOP_H);
    canvasB.createSprite(RIGHT_W, TOP_H);
    canvasInfo.createSprite(SCREEN_W, BOTTOM_H);

    unsigned long now = millis();
    aLast = bLast = infoLast = serialLast = now;
}

void loop() {
    M5Cardputer.update();

    // キー: [h] で重い処理を入れる／外す
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        for (auto c : M5Cardputer.Keyboard.keysState().word) {
            if (c == 'h') {
                heavy = !heavy;
                Serial.printf("heavy: %s\n", heavy ? "ON" : "OFF");
            }
        }
    }

    // 自分で時計を見て、時間が来た仕事を順番に呼ぶ（UNO 流）。
    // どれかの仕事が長くかかると、そのあいだ他の仕事は呼ばれない。
    unsigned long now = millis();
    if (now - aLast >= A_INTERVAL_MS) {
        aGap = now - aLast;
        aLast = now;
        jobA();
    }
    now = millis();
    if (now - bLast >= B_INTERVAL_MS) {
        bGap = now - bLast;
        bLast = now;
        jobB();
    }
    now = millis();
    if (now - infoLast >= INFO_INTERVAL_MS) {
        infoLast = now;
        drawInfo();
    }
    // シリアルは1秒に1回だけ。書き続けると次の書き込みが失敗しやすい（esptool #868・#1070）
    if (now - serialLast >= SERIAL_INTERVAL_MS) {
        serialLast = now;
        Serial.printf("A runs %lu gap %lums | B runs %lu gap %lums | heavy %s\n", aRuns,
                      aGap, bRuns, bGap, heavy ? "ON" : "OFF");
    }
}
