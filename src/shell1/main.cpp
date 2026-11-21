// 小さなシェルをつくる（docs/presentation-shell.md）
// 単位1: プロンプトが出て、文字を打てて、help と clear が効く
// 単位2: run <名前> で仕事（FreeRTOS のタスク）を作り、ps で一覧する
// 単位3: stop <名前> で止め、go <名前> で続きから動かす（第4章 demo3 の「印を立てて、自分で止まる」）
// 単位4: run heavy（自分から休まない仕事）、kill <名前>（終わらせてメモリを返す）、free（空きメモリ）
//
// シェル＝人と OS のあいだの窓口。キーボードで打った1行を「命令」として読み、OS に頼んで答えを画面に出す。
// シェルそのものは loop()（OS から見れば loopTask というタスクの1つ）で動かす。
// 画面は上下2段: 上の段＝動いている仕事の窓、下の段＝シェル。
//
// キーの読み方は M5Cardputer 1.1.1 の examples/UI/REPL（ReplView.cpp）に合わせた:
//   Keyboard.isChange() && isPressed() のときに keysState() を読み、enter・backspace（後ろの1文字を消す）・word（打った文字）を見る
//   （REPL の例は del を見ているが、1.1.1 の Keyboard.cpp では del は fn＋⌫ のときだけ。ふつうの ⌫ は backspace）
//
// ps で使う関数（ESP-IDF v4.4 の FreeRTOSConfig.h で INCLUDE_… が 1 になっているのを確認）:
//   eTaskGetState（状態）・uxTaskPriorityGet（優先度）・uxTaskGetStackHighWaterMark（残りのスタック。
//   ESP-IDF 版は StackType_t が uint8_t なので、ふつうの FreeRTOS とちがって「バイト」で返る）

#include <M5Cardputer.h>

// ---- 画面の区切り ----
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int APP_H = 40;                    // 上の段: 仕事の窓
constexpr int APP_W = SCREEN_W / 2;          // 仕事1つの窓の幅（左: count、右: ball）
constexpr int SHELL_H = SCREEN_H - APP_H;    // 下の段: シェル
constexpr int LINE_H = 10;                   // Font0 は 6×8 ドット。行の間を2ドットあける
constexpr int SHELL_ROWS = SHELL_H / LINE_H; // 9行（いちばん下がプロンプト）
constexpr int SHELL_COLS = SCREEN_W / 6;     // 40文字

M5Canvas shellCanvas(&M5Cardputer.Display);

// 画面（1枚の液晶）は、シェルと仕事のタスクが別々に描くので、貼るときだけ鍵を取る（第3章）
SemaphoreHandle_t displayLock;

void pushLocked(M5Canvas& canvas, int x, int y) {
    xSemaphoreTake(displayLock, portMAX_DELAY);
    canvas.pushSprite(x, y);
    xSemaphoreGive(displayLock);
}

// ---- シェルの段: 流れていく行と、いま打っている1行 ----
struct Line {
    String text;
    uint16_t color;
};
constexpr int HISTORY = SHELL_ROWS - 1;  // プロンプトの上に出せる行の数
Line lines[HISTORY];
int lineCount = 0;
String input;                            // いま打っている1行

// 1行足す。あふれたら古い行から上へ押し出す。長い行は40文字ずつに折り返す
void printLine(const String& text, uint16_t color = WHITE) {
    String rest = text;
    do {
        String part = rest.substring(0, SHELL_COLS);
        rest = rest.substring(part.length());
        if (lineCount == HISTORY) {
            for (int i = 1; i < HISTORY; i++) lines[i - 1] = lines[i];
            lineCount--;
        }
        lines[lineCount++] = {part, color};
    } while (rest.length() > 0);
    Serial.println(text);  // シリアルにも同じものを出す（確かめ用）
}

void drawShell() {
    shellCanvas.fillSprite(BLACK);
    shellCanvas.drawFastHLine(0, 0, SCREEN_W, DARKGREY);
    shellCanvas.setFont(&fonts::Font0);
    for (int i = 0; i < lineCount; i++) {
        shellCanvas.setTextColor(lines[i].color);
        shellCanvas.drawString(lines[i].text, 0, 3 + i * LINE_H);
    }
    // プロンプト。打った文字が長いときは、後ろの部分だけ見せる
    String shown = String("> ") + input;
    if ((int)shown.length() > SHELL_COLS - 1) shown = shown.substring(shown.length() - (SHELL_COLS - 1));
    int y = 3 + HISTORY * LINE_H;
    shellCanvas.setTextColor(GREEN);
    shellCanvas.drawString(shown, 0, y);
    // カーソル（0.5秒ごとに点滅）
    if ((millis() / 500) % 2 == 0) shellCanvas.fillRect(shown.length() * 6, y, 6, 8, GREEN);
    pushLocked(shellCanvas, 0, APP_H);
}

// ---- 仕事（アプリ）: シェルから run で作る FreeRTOS のタスク ----
struct App;
using AppFn = void (*)(App&);
struct App {
    const char* name;
    uint16_t color;
    int x;                        // 上の段のどこに窓を出すか（-1 なら窓なし）
    AppFn body;                   // 仕事の中身（for(;;) で回り続ける）
    TaskHandle_t handle;          // OS が付けた名札。nullptr なら動いていない
    M5Canvas* canvas;             // 自分の窓（仕事ごとに別の紙。第3章）
    volatile bool stopReq;        // シェルからの「止まって」のお願い
    volatile bool stopped;        // 仕事が「止まる場所に入った」印（vTaskSuspend の直前に立て、起きたら下ろす）
};

void appCount(App& app);
void appBall(App& app);
void appHeavy(App& app);

App apps[] = {
    {"count", CYAN, 0, appCount, nullptr, nullptr, false, false},
    {"ball", ORANGE, APP_W, appBall, nullptr, nullptr, false, false},
    {"heavy", RED, -1, appHeavy, nullptr, nullptr, false, false},   // 窓なし。ps で見る
};
constexpr int APP_COUNT = sizeof(apps) / sizeof(apps[0]);

App* findApp(const String& name) {
    for (auto& app : apps) {
        if (name == app.name) return &app;
    }
    return nullptr;
}

// 窓の枠と名前（左上）を描く
void beginWindow(App& app) {
    app.canvas->fillSprite(BLACK);
    if (app.x > 0) app.canvas->drawFastVLine(0, 0, APP_H, DARKGREY);
    app.canvas->setFont(&fonts::Font0);
    app.canvas->setTextColor(app.color);
    app.canvas->drawString(app.name, 6, 4);
}

// 動いていない仕事の窓（灰色で名前と「-」）。シェルが描く
void drawIdleWindow(App& app) {
    beginWindow(app);
    app.canvas->setTextColor(DARKGREY);
    app.canvas->drawString("not running", 6, 20);
    pushLocked(*app.canvas, app.x, 0);
}

// 止まる場所。外から vTaskSuspend(相手) すると、相手が画面の鍵を持ったまま止まることがある（第4章）。
// そこでシェルはお願いの印 stopReq だけ立て、仕事は鍵を返したあとのここで自分を止める
void stopPoint(App& app) {
    while (app.stopReq) {
        app.stopped = true;
        if (app.canvas) {
            beginWindow(app);
            app.canvas->setTextColor(RED);
            app.canvas->drawString("STOPPED", 6, 20);
            pushLocked(*app.canvas, app.x, 0);
        }
        vTaskSuspend(NULL);       // OS に「起こされるまで順番に入れないで」と頼む → ps で SUS
        app.stopped = false;
    }
}

// count: 100ms ごとに数を1つ増やす（第2章のタスクA と同じ）
void appCount(App& app) {
    unsigned long n = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        stopPoint(app);
        n++;
        beginWindow(app);
        app.canvas->setFont(&fonts::Font4);
        app.canvas->setTextColor(WHITE);
        app.canvas->drawString(String(n), 50, 10);
        pushLocked(*app.canvas, app.x, 0);
    }
}

// ball: 20ms ごとに玉を動かす（第2章のタスクB と同じ）
void appBall(App& app) {
    constexpr int R = 6;
    float x = 50.0f, speed = 2.0f;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        stopPoint(app);
        x += speed;
        if (x < 44 + R || x > APP_W - 6 - R) {
            speed = -speed;
            x += speed;
        }
        beginWindow(app);
        app.canvas->fillCircle((int)x, 22, R, app.color);
        pushLocked(*app.canvas, app.x, 0);
    }
}

// heavy: 自分から一度も休まない（vTaskDelay を呼ばない）。demo1 の busyWork() がずっと続くのと同じ。
// それでも count・ball・シェルは動き続ける。OS が tick（1ms）のたびに、同じ優先度のタスクに順番を回すから（第2章）
void appHeavy(App& app) {
    volatile uint32_t x = 0;
    for (;;) {
        x++;
        if ((x & 0xFFFF) == 0) stopPoint(app);   // ときどき「止まって」のお願いだけは見る（休むわけではない）
    }
}

// xTaskCreatePinnedToCore に渡す入口。引数で受け取った App の中身を呼ぶ
void appEntry(void* param) {
    App& app = *static_cast<App*>(param);
    app.body(app);
}

// ---- 命令 ----
// 1行を空白で「命令の名前」と「引数」に分ける（例: "run count" → "run" と "count"）
void splitCommand(const String& lineIn, String& name, String& arg) {
    String s = lineIn;
    s.trim();
    int sp = s.indexOf(' ');
    if (sp < 0) {
        name = s;
        arg = "";
    } else {
        name = s.substring(0, sp);
        arg = s.substring(sp + 1);
        arg.trim();
    }
}

// 引数の名前の仕事を探す。見つからなければ理由を出して nullptr
App* appFromArg(const String& cmd, const String& arg) {
    if (arg.length() == 0) {
        printLine(String("usage: ") + cmd + " <name>", RED);
        return nullptr;
    }
    App* app = findApp(arg);
    if (!app) printLine(String("no such app: ") + arg + "  (count ball heavy)", RED);
    return app;
}

void cmdHelp() {
    printLine("help        this list", CYAN);
    printLine("ps          list tasks", CYAN);
    printLine("run <name>  count ball heavy", CYAN);
    printLine("stop <name> / go <name>", CYAN);
    printLine("kill <name> end it, free memory", CYAN);
    printLine("free        free memory (heap)", CYAN);
    printLine("clear       clear the shell", CYAN);
}

void cmdRun(const String& arg) {
    App* app = appFromArg("run", arg);
    if (!app) return;
    if (app->handle) {
        printLine(String(app->name) + ": already running", YELLOW);
        return;
    }
    // OS にタスクを作ってもらう。引数: 入口の関数, 名前, スタック（バイト）, 引数, 優先度, 名札の置き場所, コア
    // 第2〜5章と同じく core 1・優先度 1（シェルの loopTask と同じ）
    BaseType_t ok = xTaskCreatePinnedToCore(appEntry, app->name, 4096, app, 1, &app->handle, 1);
    if (ok == pdPASS) {
        printLine(String("started ") + app->name, GREEN);
    } else {
        app->handle = nullptr;
        printLine(String("could not start ") + app->name, RED);
    }
}

// 動いている仕事を引数から探す。動いていなければ理由を出して nullptr
App* runningAppFromArg(const String& cmd, const String& arg) {
    App* app = appFromArg(cmd, arg);
    if (app && !app->handle) {
        printLine(String(app->name) + ": not running  (run " + app->name + ")", YELLOW);
        return nullptr;
    }
    return app;
}

void cmdStop(const String& arg) {
    App* app = runningAppFromArg("stop", arg);
    if (!app) return;
    if (app->stopReq) {
        printLine(String(app->name) + ": already stopped", YELLOW);
        return;
    }
    app->stopReq = true;   // お願いの印だけ。仕事が次に stopPoint() に来たとき自分で止まる
    printLine(String("stopping ") + app->name, GREEN);
}

void cmdGo(const String& arg) {
    App* app = runningAppFromArg("go", arg);
    if (!app) return;
    if (!app->stopReq) {
        printLine(String(app->name) + ": not stopped", YELLOW);
        return;
    }
    app->stopReq = false;
    // 仕事が「止まる場所に入った」のに、まだ vTaskSuspend まで行っていない（STOPPED を描いている途中など）と、
    // ここで vTaskResume しても空振りし、そのあと止まったきりになる。止まりきるのを少し待ってから起こす
    for (int i = 0; i < 100 && app->stopped && eTaskGetState(app->handle) != eSuspended; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (app->stopped) vTaskResume(app->handle);   // OS に「また順番に入れて」と頼む
    printLine(String("going ") + app->name, GREEN);
}

// kill: 外から vTaskDelete(相手) すると、相手が画面の鍵を持ったまま消えることがある（鍵が返らない）。
// そこで stop と同じく、まず安全な場所で止まってもらい、止まりきった（SUS）のを見てから消す。
// 止まっている別のタスクを消すと、FreeRTOS はそのタスクのスタックと TCB をその場で返す
void cmdKill(const String& arg) {
    App* app = runningAppFromArg("kill", arg);
    if (!app) return;
    app->stopReq = true;
    for (int i = 0; i < 500 && !(app->stopped && eTaskGetState(app->handle) == eSuspended); i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!(app->stopped && eTaskGetState(app->handle) == eSuspended)) {
        printLine(String(app->name) + ": could not stop, not killed", RED);
        return;
    }
    vTaskDelete(app->handle);   // OS に「このタスクを消して、メモリを返して」と頼む
    app->handle = nullptr;
    app->stopReq = false;
    app->stopped = false;
    if (app->canvas) drawIdleWindow(*app);
    printLine(String("killed ") + app->name, GREEN);
}

void cmdFree() {
    printLine(String("free heap: ") + esp_get_free_heap_size() + " bytes", CYAN);
}

const char* stateName(eTaskState s) {
    switch (s) {
        case eRunning: return "RUN";
        case eReady: return "RDY";
        case eBlocked: return "BLK";
        case eSuspended: return "SUS";
        case eDeleted: return "DEL";
        default: return "?";
    }
}

// 1行分: 名前・状態・優先度・残りのスタック（いちばん少なかったときの空き、バイト）
void psRow(const char* name, TaskHandle_t h, uint16_t color) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%-8s %-3s  %u  %6u", name, stateName(eTaskGetState(h)),
             (unsigned)uxTaskPriorityGet(h), (unsigned)uxTaskGetStackHighWaterMark(h));
    printLine(buf, color);
}

void cmdPs() {
    printLine("NAME     ST  PRI  STACK", DARKGREY);
    // シェル自身（loopTask）。ps を打った瞬間に動いているのはシェルなので、いつも RUN
    psRow("shell", xTaskGetCurrentTaskHandle(), GREEN);
    for (auto& app : apps) {
        if (app.handle) psRow(app.name, app.handle, app.color);
    }
}

void cmdClear() {
    lineCount = 0;
}

void runCommand(const String& lineIn) {
    printLine(String("> ") + lineIn, GREEN);  // 打った命令を、答えの上に残す
    String name, arg;
    splitCommand(lineIn, name, arg);
    if (name.length() == 0) return;          // 空の Enter は何もしない
    if (name == "help") {
        cmdHelp();
    } else if (name == "ps") {
        cmdPs();
    } else if (name == "run") {
        cmdRun(arg);
    } else if (name == "stop") {
        cmdStop(arg);
    } else if (name == "go") {
        cmdGo(arg);
    } else if (name == "kill") {
        cmdKill(arg);
    } else if (name == "free") {
        cmdFree();
    } else if (name == "clear") {
        cmdClear();
    } else {
        printLine(String("unknown: ") + name + "  (try help)", RED);
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;  // 指定しないと Serial.begin されない（単位0で分かったこと）
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);

    shellCanvas.createSprite(SCREEN_W, SHELL_H);
    displayLock = xSemaphoreCreateMutex();
    for (auto& app : apps) {
        if (app.x < 0) continue;   // 窓のない仕事（heavy）
        app.canvas = new M5Canvas(&M5Cardputer.Display);
        app.canvas->createSprite(APP_W, APP_H);
        drawIdleWindow(app);
    }

    printLine("cardputer shell v0.4", CYAN);
    printLine("type help and press Enter", DARKGREY);
    drawShell();
}

void loop() {
    static unsigned long drawLast = 0;
    bool changed = false;

    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto& status = M5Cardputer.Keyboard.keysState();
        if (status.enter) {
            String cmd = input;
            input = "";
            runCommand(cmd);
        } else if (status.backspace || status.del) {
            if (input.length() > 0) input.remove(input.length() - 1);
        } else {
            for (auto c : status.word) input += c;
        }
        changed = true;
    }

    // 打ったらすぐ、打たなくてもカーソルの点滅のために 250ms ごとに描く
    unsigned long now = millis();
    if (changed || now - drawLast >= 250) {
        drawLast = now;
        drawShell();
    }
    delay(10);  // 10ms 休んで、ほかのタスクに順番をゆずる
}
