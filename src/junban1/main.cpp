// 順番係をつくる（1段目: 協力型）
//
// FreeRTOS のタスクは使わず、自作の「順番係」が仕事A（数）と仕事B（玉）を交代で呼ぶ。
//   - 係は「帳面」（Task の配列）に、各仕事の状態と「どこまでやったか」の印（step）を書き留めておく
//     画面の帳面: RUN = いま係が呼んでいる / RDY = 順番待ち / WAIT = 寝ている
//     gap = 最後の1回の間隔、max = 前の1秒でいちばん長かった間隔（ときどきだけ長く待たされるのを見るため）
//   - 係は順番に仕事を1つ呼ぶ。仕事は1歩（step）だけ進めて、すぐ係に返す（＝「次どうぞ」）
//   - 仕事が「寝る」ときは、起きる時刻を帳面に書いて WAIT になる。係は時刻が来たら READY に戻す
// [h] 仕事A に重い処理（300ms）を入れる／外す。小分けにしていないと、300ms のあいだ係に順番が戻らない
// [s] 重い処理を 10ms × 30回に小分けにする／戻す。1回ごとに係に返すので、仕事B にも順番が回る
//
// 協力型なので、係は仕事の途中で順番を取り上げられない。仕事が自分で返してくれるのを待つしかない。
// （取り上げる＝割り込み型は、次のバージョンの2段目）
// ここは loop() の中だけで動く。FreeRTOS から見れば、loopTask という1つのタスクの中の出来事。

#include <M5Cardputer.h>

// ---- 画面の区切り（「ふたつのタスク」と同じ） ----
constexpr int SCREEN_W = 240;
constexpr int TOP_H = 85;
constexpr int LEFT_W = 110;
constexpr int RIGHT_W = SCREEN_W - LEFT_W;
constexpr int BOTTOM_H = 135 - TOP_H;

M5Canvas canvasA(&M5Cardputer.Display);
M5Canvas canvasB(&M5Cardputer.Display);
M5Canvas canvasInfo(&M5Cardputer.Display);

// ---- 帳面（係が覚えていること） ----
enum State : uint8_t { READY, WAIT };

struct Task {
    const char* name;
    State state;
    uint8_t step;            // どこまでやったか（印）。仕事は次に呼ばれたとき、この続きから始める
    uint8_t steps;           // この回の全部の歩数（画面に「step 3/5」と出す用）
    unsigned long wakeAt;    // WAIT のとき、起きる時刻（millis）
    unsigned long runs;      // 1回分の仕事を終えた回数
    unsigned long last;      // 前の回を始めた時刻（gap を出す用）
    unsigned long gap;       // 前の回からの間隔（ms）。最後の1回だけ
    unsigned long gapMax;    // この1秒でいちばん長かった間隔（集めている途中）
    unsigned long gapMaxShown; // 前の1秒でいちばん長かった間隔（画面に出す用）
    void (*fn)(Task&);       // 1歩だけ進める関数
};

void jobA(Task& t);
void jobB(Task& t);

Task tasks[] = {
    {"A", READY, 0, 2, 0, 0, 0, 0, 0, 0, jobA},
    {"B", READY, 0, 2, 0, 0, 0, 0, 0, 0, jobB},
};
constexpr int TASK_COUNT = sizeof(tasks) / sizeof(tasks[0]);
int current = -1;               // いま呼んでいる仕事（帳面の番号）。-1 はだれも呼んでいない
unsigned long switches = 0;     // 係が仕事を呼んだ回数

// 仕事が「寝る」ときに呼ぶ。起きる時刻を帳面に書き、印を最初に戻す
void sleepFor(Task& t, unsigned long ms) {
    t.state = WAIT;
    t.wakeAt = millis() + ms;
    t.step = 0;
}

// 仕事が新しい回を始めるときに、回数と gap を帳面に書く
void beginRound(Task& t) {
    unsigned long now = millis();
    if (t.runs > 0) {
        t.gap = now - t.last;
        if (t.gap > t.gapMax) t.gapMax = t.gap;
    }
    t.last = now;
    t.runs++;
}

// ---- 重い処理 ----
constexpr unsigned long HEAVY_MS = 300;
constexpr int CHUNKS = 30;                       // 小分けにしたときの回数（10ms × 30回）
bool heavy = false;
bool split = false;

void busyWork(unsigned long ms) {                // 「ふたつのタスク」と同じ。そのあいだ CPU を使い切る
    unsigned long start = millis();
    volatile uint32_t x = 0;
    while (millis() - start < ms) {
        x++;
    }
}

// ---- 仕事A: 数を数える ----
// 印（step）の意味: 0 = 数を増やす / 1〜 = 重い処理（あれば）/ 最後 = 描く
void jobA(Task& t) {
    int heavySteps = heavy ? (split ? CHUNKS : 1) : 0;
    t.steps = 2 + heavySteps;
    if (t.step == 0) {
        beginRound(t);
        t.step = 1;
        return;                                  // 1歩進めたので係に返す（次どうぞ）
    }
    if (t.step <= heavySteps) {
        busyWork(split ? HEAVY_MS / CHUNKS : HEAVY_MS);   // 小分けなら 10ms、そうでなければ 300ms 居座る
        t.step++;
        return;
    }
    canvasA.fillSprite(BLACK);
    canvasA.setFont(&fonts::Font2);
    canvasA.setTextColor(CYAN);
    canvasA.drawString("job A", 8, 6);
    canvasA.setFont(&fonts::Font4);
    canvasA.setTextColor(WHITE);
    canvasA.drawString(String(t.runs), 8, 38);
    canvasA.pushSprite(0, 0);
    sleepFor(t, 100);
}

// ---- 仕事B: 玉を動かす ----
// 印（step）の意味: 0 = 玉を動かす / 1 = 描く
float ballX = 20.0f;
float ballSpeed = 2.5f;
constexpr int BALL_R = 8;

void jobB(Task& t) {
    t.steps = 2;
    if (t.step == 0) {
        beginRound(t);
        ballX += ballSpeed;
        if (ballX < BALL_R + 4) { ballX = BALL_R + 4; ballSpeed = -ballSpeed; }
        else if (ballX > RIGHT_W - BALL_R - 4) { ballX = RIGHT_W - BALL_R - 4; ballSpeed = -ballSpeed; }
        t.step = 1;
        return;
    }
    canvasB.fillSprite(BLACK);
    canvasB.drawFastVLine(0, 0, TOP_H, DARKGREY);
    canvasB.setFont(&fonts::Font2);
    canvasB.setTextColor(ORANGE);
    canvasB.drawString("job B", 8, 6);
    canvasB.fillCircle((int)ballX, 52, BALL_R, ORANGE);
    canvasB.pushSprite(LEFT_W, 0);
    sleepFor(t, 20);
}

// ---- 順番係 ----
// 1回呼ばれるごとに、時刻が来た仕事を起こし、次の READY の仕事を1つだけ呼ぶ
void junbanStep() {
    unsigned long now = millis();
    for (int i = 0; i < TASK_COUNT; i++) {
        if (tasks[i].state == WAIT && (long)(now - tasks[i].wakeAt) >= 0) {
            tasks[i].state = READY;              // 時刻が来たので、順番待ちに戻す
        }
    }
    for (int k = 1; k <= TASK_COUNT; k++) {      // 前に呼んだ仕事の次から探す（順番に回す）
        int i = (current + k + TASK_COUNT) % TASK_COUNT;
        if (tasks[i].state == READY) {
            current = i;
            switches++;
            tasks[i].fn(tasks[i]);               // 1歩だけ進めてもらう。返ってくるまで係は待つしかない
            return;
        }
    }
    current = -1;                                // だれも READY でなければ、何もしない
}

// ---- 帳面を画面の下に描く ----
void drawLedger() {
    canvasInfo.fillSprite(BLACK);
    canvasInfo.drawFastHLine(0, 0, SCREEN_W, DARKGREY);
    canvasInfo.setFont(&fonts::Font2);
    const uint16_t colors[] = {CYAN, ORANGE};
    for (int i = 0; i < TASK_COUNT; i++) {
        Task& t = tasks[i];
        canvasInfo.setTextColor(colors[i]);
        canvasInfo.drawString(String("#") + (i + 1) + " " + t.name + " " + (i == current ? "RUN " : t.state == READY ? "RDY " : "WAIT") +
                                  " " + t.step + "/" + t.steps + " gap" + t.gap + " max" + t.gapMaxShown,
                              4, 2 + i * 16);
    }
    canvasInfo.setTextColor(heavy ? RED : GREEN);
    canvasInfo.drawString(String("sw:") + switches + " [h]" + (heavy ? "ON" : "off") + " [s]" + (split ? "ON" : "off"), 4, 34);
    canvasInfo.pushSprite(0, TOP_H);
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;  // 「ふたつのタスク」の単位0で分かったこと: 指定しないと Serial.begin されない
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);
    canvasA.createSprite(LEFT_W, TOP_H);
    canvasB.createSprite(RIGHT_W, TOP_H);
    canvasInfo.createSprite(SCREEN_W, BOTTOM_H);
}

void loop() {
    static unsigned long ledgerLast = 0;
    static unsigned long serialLast = 0;

    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        for (auto c : M5Cardputer.Keyboard.keysState().word) {
            if (c == 'h') { heavy = !heavy; Serial.printf("heavy: %s\n", heavy ? "ON" : "OFF"); }
            if (c == 's') { split = !split; Serial.printf("split: %s\n", split ? "ON" : "OFF"); }
        }
    }

    junbanStep();   // 係に1回働いてもらう（仕事を1歩だけ進める）

    unsigned long now = millis();
    if (now - ledgerLast >= 200) {
        ledgerLast = now;
        drawLedger();
    }
    if (now - serialLast >= 1000) {
        serialLast = now;
        // 1秒ごとに「いちばん長かった間隔」を締めて、画面用に写す
        for (int i = 0; i < TASK_COUNT; i++) {
            tasks[i].gapMaxShown = tasks[i].gapMax;
            tasks[i].gapMax = 0;
        }
        Serial.printf("A runs %lu gap %lums max %lums | B runs %lu gap %lums max %lums | switches %lu | heavy %s split %s\n",
                      tasks[0].runs, tasks[0].gap, tasks[0].gapMaxShown, tasks[1].runs, tasks[1].gap,
                      tasks[1].gapMaxShown, switches, heavy ? "ON" : "OFF", split ? "ON" : "OFF");
    }
}
