// デモ3: キーで片方のタスクを止める（デモ2に足したもの）
//
// [1] でタスクA、[2] でタスクB を止める／また動かす。止めたほうの数・玉はそこで止まり、もう片方は動き続ける。
// 止めるのは OS の vTaskSuspend()、動かすのは vTaskResume()。止まったタスクは OS の「順番待ちの列」から外される。
//
// 注意: 外から vTaskSuspend(相手) で止めると、相手が画面の鍵を持ったまま止まることがある。
// そうなると、だれも画面を使えなくなる（デッドロック）。そこで「止まって」とお願いの印だけ立て、
// タスク自身が鍵を返したあとの安全な場所で vTaskSuspend(NULL)（＝自分を止める）を呼ぶ。
//
// ---- 以下はデモ2の説明 ----
// 見た目はデモ1と同じ。ちがうのは「だれが順番を回すか」。
//   デモ1: loop() の中で自分が millis() を見て、仕事A → 仕事B と順に呼んでいた
//   デモ2: 仕事A と仕事B をそれぞれ「タスク」にして OS（FreeRTOS）に渡す。順番は OS が回す
// [h] で仕事A に同じ重い処理（300ms）を入れても、OS は約1ms ごとに交代させるので、仕事B の玉はなめらかなまま。
//
// 交代の様子をはっきり見せるため、2つのタスクはわざと同じコア（core 1、loop() と同じ）・同じ優先度に置く。
// （コア0にはウォッチドッグがあり、重い処理で止められる。Arduino 2.0.16 の sdkconfig: CHECK_IDLE_TASK_CPU1 は無効）

#include <M5Cardputer.h>

// ---- 画面の区切り（デモ1と同じ） ----
constexpr int SCREEN_W = 240;
constexpr int TOP_H = 85;
constexpr int LEFT_W = 110;
constexpr int RIGHT_W = SCREEN_W - LEFT_W;
constexpr int BOTTOM_H = 135 - TOP_H;

M5Canvas canvasA(&M5Cardputer.Display);
M5Canvas canvasB(&M5Cardputer.Display);
M5Canvas canvasInfo(&M5Cardputer.Display);

// 画面（SPI でつながった1枚の液晶）は、同時に2つのタスクが描くと絵が壊れる。
// 画面へ貼るときだけ「鍵」（ミューテックス）を取り、1つずつ順に使う
SemaphoreHandle_t displayLock;

void pushLocked(M5Canvas& canvas, int x, int y) {
    xSemaphoreTake(displayLock, portMAX_DELAY);
    canvas.pushSprite(x, y);
    xSemaphoreGive(displayLock);
}

// ---- 仕事A・仕事B の記録（タスクと loop() の両方から読むので volatile） ----
constexpr uint32_t A_INTERVAL_MS = 100;
constexpr uint32_t B_INTERVAL_MS = 20;
volatile unsigned long aRuns = 0, aGap = 0;
volatile unsigned long bRuns = 0, bGap = 0;
volatile int aCore = -1, bCore = -1;

constexpr unsigned long HEAVY_MS = 300;
volatile bool heavy = false;

// 止めるお願いの印と、タスクの名札（ハンドル。vTaskResume で相手を指すのに使う）
volatile bool stopA = false, stopB = false;
TaskHandle_t taskAHandle = nullptr, taskBHandle = nullptr;

// 止まる直前に、自分の区切りに STOPPED と書いておく
void drawStopped(M5Canvas& canvas, const char* label, uint16_t color, int x) {
    canvas.fillSprite(BLACK);
    if (x > 0) canvas.drawFastVLine(0, 0, TOP_H, DARKGREY);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(color);
    canvas.drawString(label, 8, 6);
    canvas.setTextColor(RED);
    canvas.drawString("STOPPED", 8, 44);
    pushLocked(canvas, x, 0);
}

// 計算のまね（デモ1と同じ）。OS に「休みます」と言わずに CPU を使い続ける。
// それでも OS は時間が来たら割り込んで、ほかのタスクに順番を回す
void busyWork(unsigned long ms) {
    unsigned long start = millis();
    volatile uint32_t x = 0;
    while (millis() - start < ms) {
        x++;
    }
}

// ---- タスクA: 数を数える ----
void taskA(void*) {
    unsigned long prev = millis();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(A_INTERVAL_MS));  // 「100ms 寝かせて」と OS に頼む。その間ほかのタスクが動く
        // ここは鍵を持っていない安全な場所。お願いが来ていたら自分を止める
        while (stopA) {
            drawStopped(canvasA, "task A", CYAN, 0);
            vTaskSuspend(NULL);   // OS に「起こされるまで順番に入れないで」と頼む
            prev = millis();      // 止まっていた時間を gap に数えない
        }
        unsigned long now = millis();
        aGap = now - prev;
        prev = now;
        aRuns++;
        aCore = xPortGetCoreID();
        if (heavy) {
            busyWork(HEAVY_MS);
        }
        canvasA.fillSprite(BLACK);
        canvasA.setFont(&fonts::Font2);
        canvasA.setTextColor(CYAN);
        canvasA.drawString("task A", 8, 6);
        canvasA.setFont(&fonts::Font4);
        canvasA.setTextColor(WHITE);
        canvasA.drawString(String(aRuns), 8, 38);
        pushLocked(canvasA, 0, 0);
    }
}

// ---- タスクB: 玉を動かす ----
void taskB(void*) {
    float ballX = 20.0f;
    float ballSpeed = 2.5f;
    constexpr int BALL_R = 8;
    unsigned long prev = millis();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(B_INTERVAL_MS));
        while (stopB) {
            drawStopped(canvasB, "task B", ORANGE, LEFT_W);
            vTaskSuspend(NULL);
            prev = millis();
        }
        unsigned long now = millis();
        bGap = now - prev;
        prev = now;
        bRuns++;
        bCore = xPortGetCoreID();
        ballX += ballSpeed;
        if (ballX < BALL_R + 4) {
            ballX = BALL_R + 4;
            ballSpeed = -ballSpeed;
        } else if (ballX > RIGHT_W - BALL_R - 4) {
            ballX = RIGHT_W - BALL_R - 4;
            ballSpeed = -ballSpeed;
        }
        canvasB.fillSprite(BLACK);
        canvasB.drawFastVLine(0, 0, TOP_H, DARKGREY);
        canvasB.setFont(&fonts::Font2);
        canvasB.setTextColor(ORANGE);
        canvasB.drawString("task B", 8, 6);
        canvasB.fillCircle((int)ballX, 52, BALL_R, ORANGE);
        pushLocked(canvasB, LEFT_W, 0);
    }
}

void drawInfo() {
    canvasInfo.fillSprite(BLACK);
    canvasInfo.drawFastHLine(0, 0, SCREEN_W, DARKGREY);
    canvasInfo.setFont(&fonts::Font2);
    canvasInfo.setTextColor(WHITE);
    canvasInfo.setTextColor(stopA ? RED : WHITE);
    canvasInfo.drawString(String("A:") + aRuns + " gap" + aGap + "ms c" + aCore + "  [1]" + (stopA ? "STOP" : "RUN"), 4, 3);
    canvasInfo.setTextColor(stopB ? RED : WHITE);
    canvasInfo.drawString(String("B:") + bRuns + " gap" + bGap + "ms c" + bCore + "  [2]" + (stopB ? "STOP" : "RUN"), 4, 18);
    canvasInfo.setTextColor(heavy ? RED : GREEN);
    canvasInfo.drawString(String("[h] heavy: ") + (heavy ? "ON" : "OFF"), 4, 33);
    pushLocked(canvasInfo, 0, TOP_H);
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;  // 単位0で分かったこと: 指定しないと Serial.begin されない
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);

    canvasA.createSprite(LEFT_W, TOP_H);
    canvasB.createSprite(RIGHT_W, TOP_H);
    canvasInfo.createSprite(SCREEN_W, BOTTOM_H);

    displayLock = xSemaphoreCreateMutex();

    // タスクを2つ作って OS に渡す。引数: 関数, 名前, スタックの大きさ, 引数, 優先度, ハンドル, コア
    // 優先度 1 は loop() と同じ。3つとも同じ優先度・同じコアなので、OS が順番に交代させる
    xTaskCreatePinnedToCore(taskA, "taskA", 8192, nullptr, 1, &taskAHandle, 1);
    xTaskCreatePinnedToCore(taskB, "taskB", 8192, nullptr, 1, &taskBHandle, 1);
}

// loop() も OS から見ればタスクの1つ（loopTask）。ここではキーと下の段だけを受け持つ
void loop() {
    static unsigned long infoLast = 0;
    static unsigned long serialLast = 0;

    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        for (auto c : M5Cardputer.Keyboard.keysState().word) {
            if (c == 'h') {
                heavy = !heavy;
                Serial.printf("heavy: %s\n", heavy ? "ON" : "OFF");
            } else if (c == '1') {
                stopA = !stopA;
                if (!stopA) vTaskResume(taskAHandle);  // OS に「A をまた順番に入れて」と頼む
                Serial.printf("task A: %s\n", stopA ? "STOP" : "RUN");
            } else if (c == '2') {
                stopB = !stopB;
                if (!stopB) vTaskResume(taskBHandle);
                Serial.printf("task B: %s\n", stopB ? "STOP" : "RUN");
            }
        }
    }

    unsigned long now = millis();
    if (now - infoLast >= 200) {
        infoLast = now;
        drawInfo();
    }
    // シリアルは1秒に1回だけ（esptool #868・#1070 の対策の名残。4.9.0 では不要かもしれないが少なめにしておく）
    if (now - serialLast >= 1000) {
        serialLast = now;
        Serial.printf("A runs %lu gap %lums core%d %s | B runs %lu gap %lums core%d %s | heavy %s\n",
                      aRuns, aGap, aCore, stopA ? "STOP" : "RUN", bRuns, bGap, bCore,
                      stopB ? "STOP" : "RUN", heavy ? "ON" : "OFF");
    }
    delay(10);  // 10ms 休んで、ほかのタスクに順番をゆずる
}
