// v1.1: シェルの表示を日本語対応（efontJA_10、字の切れ目で折り返し）
// v1.1 単位3: 送りがな・学習（/.ime-learn）・Enter で /kana.txt に書き足す
// v1.1 単位2: 漢字（SKK-JISYO.M を埋め込み、二分探索、▽▼）
// v1.1 単位1: 日本語入力 ひらがな（ime.cpp・kana.cpp）、ホームに Kana
// v1.0 単位2: /.last から前の画面を戻す（restore）
// v1.0 単位1: /.last に今の画面を書く（save、デバウンス）
// v0.9 単位3: 戻る場所の積み重ね（openApp・goBack、resume）。メモ帳を memoApp に
// v0.9 単位2: ファイラー（filer.cpp）
// v0.9 単位1: ホーム画面（home.cpp）と前面のアプリ（uiapp.h の UiApp: key・draw）
// メモ帳 v0.7（edit <ファイル>）。src/shell3（v0.6、教科書 第10章）を写して広げたもの
// v0.8: 履歴を /.history に残す（lineedit.cpp）。ls は . で始まる名前を出さない
// v0.7 単位1: edit で開いて見る（editor.cpp）。仕事 count・ball と上の段の窓はなくし、画面ぜんぶをシェルとメモ帳に使う
//
// ---- 以下は v0.6 の説明 ----
// ファイルを書く（docs/presentation-shell3.md）v0.6
// v0.6 単位1: echo（> で書き直し、>> で足す）・rm（files.cpp）
// v0.6 単位2: mkdir・cd・pwd。プロンプトに、いまいるフォルダを出す（/notes> ）
//
// ---- 以下は v0.5 の説明 ----
// シェルを広げる（docs/presentation-shell2.md）。src/shell1（v0.4、教科書 第8章）を写して広げたもの
// v0.5 単位2: ls・cat（内蔵フラッシュの LittleFS。files.cpp）
// v0.5 単位1: Tab で補う（命令の名前・仕事の名前）、fn＋; / fn＋. で履歴をさかのぼる・戻る、history
//   キー: M5Cardputer（GitHub の main、コミット bf64d31。タグ 1.1.1 には ↑↓ が無い）の Keyboard.cpp では、fn を押しているときは fn の層（; → KEY_UP、. → KEY_DOWN）だけを見て
//   word（打った文字）には何も入れない。Tab は専用のキーで keysState().tab が立つ
//
// ---- 以下は v0.4 の説明 ----
// 小さなシェルをつくる（docs/presentation-shell.md）
// 単位1: プロンプトが出て、文字を打てて、help と clear が効く
// 単位2: run <名前> で仕事（FreeRTOS のタスク）を作り、ps で一覧する
// 単位3: stop <名前> で止め、go <名前> で続きから動かす（第4章 demo3 の「印を立てて、自分で止まる」）
// 単位4: run heavy（自分から休まない仕事）、kill <名前>（終わらせてメモリを返す）、free（空きメモリ）
//
// シェル＝人と OS のあいだの窓口。キーボードで打った1行を「命令」として読み、OS に頼んで答えを画面に出す。
// シェルそのものは loop()（OS から見れば loopTask というタスクの1つ）で動かす。
// 画面はぜんぶシェル（v0.7 から。edit の間はメモ帳が使う）。
//
// キーの読み方は M5Cardputer 1.1.1 の examples/UI/REPL（ReplView.cpp）に合わせた:
//   Keyboard.isChange() && isPressed() のときに keysState() を読み、enter・backspace（後ろの1文字を消す）・word（打った文字）を見る
//   （REPL の例は del を見ているが、1.1.1 の Keyboard.cpp では del は fn＋⌫ のときだけ。ふつうの ⌫ は backspace）
//
// ps で使う関数（ESP-IDF v4.4 の FreeRTOSConfig.h で INCLUDE_… が 1 になっているのを確認）:
//   eTaskGetState（状態）・uxTaskPriorityGet（優先度）・uxTaskGetStackHighWaterMark（残りのスタック。
//   ESP-IDF 版は StackType_t が uint8_t なので、ふつうの FreeRTOS とちがって「バイト」で返る）

#include <M5Cardputer.h>
#include <LittleFS.h>
#include "shell.h"
#include "uiapp.h"
#include "ime.h"

// ---- 画面の区切り ----
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int LINE_H = 10;                   // Font0 は 6×8 ドット。行の間を2ドットあける
constexpr int SHELL_ROWS = SCREEN_H / LINE_H; // 13行（いちばん下がプロンプト）
constexpr int SHELL_COLS = SCREEN_W / 6;     // 40文字

M5Canvas shellCanvas(&M5Cardputer.Display);

// v0.7 から画面に描くのはシェル（loop）だけになったので、画面の鍵（第3章）は使わない

// ---- シェルの段: 流れていく行と、いま打っている1行 ----
struct Line {
    String text;
    uint16_t color;
    bool ja;  // 日本語のフォント（efontJA_10）で描くか
};
constexpr int HISTORY = SHELL_ROWS - 1;  // プロンプトの上に出せる行の数
Line lines[HISTORY];
int lineCount = 0;
String input;                            // いま打っている1行


// 1行足す。あふれたら古い行から上へ押し出す。長い行は画面の幅で折り返す
// 日本語の字は Font0 に無いので efontJA_10 で描く。UTF-8 の1文字は日本語なら3バイトなので、
// バイトではなく字の切れ目で、画面の幅で折り返す
void printLine(const String& text, uint16_t color = WHITE) {
    bool ja = false;  // 0x80 以上のバイト（UTF-8 の2バイト以上の字）があれば日本語のフォント
    for (unsigned int i = 0; i < text.length(); i++) {
        if ((uint8_t)text[i] >= 0x80) { ja = true; break; }
    }
    if (ja) shellCanvas.setFont(&fonts::efontJA_10);
    else shellCanvas.setFont(&fonts::Font0);
    String rest = text;
    do {
        // 字の切れ目（続きのバイト 10xxxxxx でない所）ごとに進め、幅に入るだけ取る
        unsigned int cut = 0;
        while (cut < rest.length()) {
            unsigned int next = cut + 1;
            while (next < rest.length() && ((uint8_t)rest[next] & 0xC0) == 0x80) next++;
            if (shellCanvas.textWidth(rest.substring(0, next)) > SCREEN_W) break;
            cut = next;
        }
        if (cut == 0 && rest.length() > 0) {  // 1字も入らないときも、1字は出して先へ進む
            cut = 1;
            while (cut < rest.length() && ((uint8_t)rest[cut] & 0xC0) == 0x80) cut++;
        }
        String part = rest.substring(0, cut);
        rest = rest.substring(cut);
        if (lineCount == HISTORY) {
            for (int i = 1; i < HISTORY; i++) lines[i - 1] = lines[i];
            lineCount--;
        }
        lines[lineCount++] = {part, color, ja};
    } while (rest.length() > 0);
    Serial.println(text);  // シリアルにも同じものを出す（確かめ用）
}

void drawShell(M5Canvas& canvas) {
    canvas.fillSprite(BLACK);
    for (int i = 0; i < lineCount; i++) {
        if (lines[i].ja) canvas.setFont(&fonts::efontJA_10);
        else canvas.setFont(&fonts::Font0);
        canvas.setTextColor(lines[i].color);
        canvas.drawString(lines[i].text, 0, 3 + i * LINE_H);
    }
    canvas.setFont(&fonts::Font0);
    // プロンプト。打った文字が長いときは、後ろの部分だけ見せる
    String shown = currentDir() + "> " + input;
    if ((int)shown.length() > SHELL_COLS - 1) shown = shown.substring(shown.length() - (SHELL_COLS - 1));
    int y = 3 + HISTORY * LINE_H;
    canvas.setTextColor(GREEN);
    canvas.drawString(shown, 0, y);
    // カーソル（0.5秒ごとに点滅）
    if ((millis() / 500) % 2 == 0) canvas.fillRect(shown.length() * 6, y, 6, 8, GREEN);
}

// ---- 仕事（アプリ）: シェルから run で作る FreeRTOS のタスク ----
struct App;
using AppFn = void (*)(App&);
struct App {
    const char* name;
    uint16_t color;
    AppFn body;                   // 仕事の中身（for(;;) で回り続ける）
    TaskHandle_t handle;          // OS が付けた名札。nullptr なら動いていない
    volatile bool stopReq;        // シェルからの「止まって」のお願い
    volatile bool stopped;        // 仕事が「止まる場所に入った」印（vTaskSuspend の直前に立て、起きたら下ろす）
};

void appHeavy(App& app);

// v0.7 で count・ball はなくした（窓の要る仕事がなくなり、画面ぜんぶをシェルとメモ帳に使う）
App apps[] = {
    {"heavy", RED, appHeavy, nullptr, false, false},   // 窓なし。ps で見る
};
constexpr int APP_COUNT = sizeof(apps) / sizeof(apps[0]);
int appCount() { return APP_COUNT; }
const char* appName(int i) { return apps[i].name; }

App* findApp(const String& name) {
    for (auto& app : apps) {
        if (name == app.name) return &app;
    }
    return nullptr;
}

// 止まる場所。外から vTaskSuspend(相手) すると、相手が画面の鍵を持ったまま止まることがある（第4章）。
// そこでシェルはお願いの印 stopReq だけ立て、仕事は鍵を返したあとのここで自分を止める
void stopPoint(App& app) {
    while (app.stopReq) {
        app.stopped = true;
        vTaskSuspend(NULL);       // OS に「起こされるまで順番に入れないで」と頼む → ps で SUS
        app.stopped = false;
    }
}

// heavy: 自分から一度も休まない（vTaskDelay を呼ばない）。demo1 の busyWork() がずっと続くのと同じ。
// それでもシェルは動き続ける。OS が tick（1ms）のたびに、同じ優先度のタスクに順番を回すから（第2章）
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
// 1行を空白で「命令の名前」と「引数」に分ける（例: "run heavy" → "run" と "heavy"）
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
    if (!app) printLine(String("no such app: ") + arg + "  (heavy)", RED);
    return app;
}

void cmdHelp() {
    printLine("ps          list tasks", CYAN);
    printLine("run <name>  heavy", CYAN);
    printLine("stop / go / kill <name>", CYAN);
    printLine("ls cat rm / echo txt > (>>) f", CYAN);
    printLine("mkdir / cd / pwd / edit <file>", CYAN);
    printLine("help / free / history / clear", CYAN);
    printLine("Tab: complete  fn+; fn+.: history", CYAN);
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
    printLine(currentDir() + "> " + lineIn, GREEN);  // 打った命令を、答えの上に残す（そのときのフォルダも）
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
    } else if (name == "ls") {
        cmdLs(arg);
    } else if (name == "cat") {
        cmdCat(arg);
    } else if (name == "mkdir") {
        cmdMkdir(arg);
    } else if (name == "cd") {
        cmdCd(arg);
    } else if (name == "pwd") {
        cmdPwd();
    } else if (name == "echo") {
        cmdEcho(arg);
    } else if (name == "rm") {
        cmdRm(arg);
    } else if (name == "edit") {
        if (editOpen(arg)) openApp(&memoApp);   // 開けたら、メモ帳を前面に出す（Esc で閉じるとシェルに戻る）
    } else if (name == "history") {
        cmdHistory();
    } else if (name == "clear") {
        cmdClear();
    } else {
        printLine(String("unknown: ") + name + "  (try help)", RED);
    }
}

void restoreLast();   // 下の「/.last から戻す」

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;  // 指定しないと Serial.begin されない（単位0で分かったこと）
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);

    shellCanvas.createSprite(SCREEN_W, SCREEN_H);

    printLine("cardputer shell v1.1", CYAN);
    printLine("type help and press Enter", DARKGREY);
    fsInit();   // 内蔵フラッシュのファイルの置き場所を使えるようにする（初回は見本のファイルを作る）
    histLoad(); // 前に打った命令を /.history から読み直す
    imeLoadLearn();  // 日本語入力で選んだ候補を /.ime-learn から読み直す（学習）
    restoreLast();  // 前に開いていた画面を /.last から戻す
}

// ---- シェルのアプリ（v0.9）: 今までの loop() の中身を key と draw に分けたもの ----
void shellKey(const Keyboard_Class::KeysState& status) {
    if (status.esc) {          // fn＋`: 前に戻る（ふつうはホーム）
        goBack();
    } else if (status.enter) {
        String cmd = input;
        input = "";
        histPos = -1;
        histPush(cmd);
        runCommand(cmd);
    } else if (status.tab) {
        complete();
    } else if (status.up) {
        histUp();
    } else if (status.down) {
        histDown();
    } else if (status.backspace || status.del) {
        if (input.length() > 0) input.remove(input.length() - 1);
    } else {
        for (auto c : status.word) input += c;
    }
}

void shellDraw(M5Canvas& canvas) {
    drawShell(canvas);
}

String shellSave() { return "shell"; }

UiApp shellApp = {"shell", shellKey, shellDraw, nullptr, shellSave, nullptr};

// ---- 前面のアプリ ----
// 1枚の画面とキーボードは、前面のアプリだけに渡す。起動するとホーム
UiApp* front = &homeApp;
bool redrawNow = true;     // 次の loop ですぐ描く印

void switchTo(UiApp* app) {
    front = app;
    redrawNow = true;
}

// 戻る場所: 開くたびに積み、Esc で1つ取り出して前に戻る（スタック）
constexpr int BACK_MAX = 8;
UiApp* backStack[BACK_MAX];
int backCount = 0;

// 並びをシリアルに出す（確かめ用）。例: "stack: home > files > memo"（最後が前面）
void printStack() {
    String s = "stack: ";
    for (int i = 0; i < backCount; i++) s += String(backStack[i]->name) + " > ";
    s += front->name;
    Serial.println(s);
}

// ---- /.last: いまの画面を残す ----
// 電源スイッチを切る瞬間には知らせが来ないので、変化のたびに印を立て、落ち着いてから1回だけ書く（デバウンス）。中身が同じなら書かない
bool lastDirty = false;
unsigned long changedAt = 0;
String lastWritten;

void markChanged() {
    lastDirty = true;
    changedAt = millis();
}

String appSave(UiApp* app) {
    return app->save ? app->save() : String(app->name);
}

// 積んだ下から順に1行ずつ、最後が前面
void writeLast() {
    String s;
    for (int i = 0; i < backCount; i++) s += appSave(backStack[i]) + "\n";
    s += appSave(front) + "\n";
    if (s == lastWritten) return;
    if (!fsIsReady()) return;
    File f = LittleFS.open("/.last", FILE_WRITE);
    if (!f) return;
    f.print(s);
    f.close();
    lastWritten = s;
    Serial.print("last: saved\n" + s);
}

// 下の行から順に戻して、戻る場所の積み重ねを組み立て直す。戻せない行があれば、その手前まで
void restoreLast() {
    if (!fsIsReady() || !LittleFS.exists("/.last")) return;   // なければホームから
    File f = LittleFS.open("/.last");
    if (!f) return;
    String text;
    while (f.available()) text += (char)f.read();
    f.close();
    lastWritten = text;   // 起動してすぐ同じ中身を書き直さない

    UiApp* all[] = {&homeApp, &shellApp, &filesApp, &memoApp, &kanaApp};
    UiApp* got[BACK_MAX + 1];
    int n = 0;
    String stoppedAt;
    int start = 0;
    while (start < (int)text.length()) {
        int nl = text.indexOf('\n', start);
        if (nl < 0) nl = text.length();
        String line = text.substring(start, nl);
        start = nl + 1;
        line.replace("\r", "");
        line.trim();
        if (line.length() == 0) continue;
        String name, arg;
        splitCommand(line, name, arg);
        UiApp* app = nullptr;
        for (auto a : all) {
            if (name == a->name) app = a;
        }
        if (!app || (app->restore && !app->restore(arg))) {
            stoppedAt = line;
            break;
        }
        if (n == BACK_MAX + 1) {   // 多すぎたら、いちばん古いものを捨てる（8段まで＋前面）
            for (int i = 1; i < n; i++) got[i - 1] = got[i];
            n--;
        }
        got[n++] = app;
    }

    if (n > 0) {
        backCount = n - 1;
        for (int i = 0; i < backCount; i++) backStack[i] = got[i];
        switchTo(got[n - 1]);
    }
    String s = "last: restored ";
    for (int i = 0; i < backCount; i++) s += String(backStack[i]->name) + " > ";
    s += front->name;
    Serial.println(s);
    if (stoppedAt.length() > 0) Serial.println("last: stopped at " + stoppedAt);
}

void openApp(UiApp* app) {
    if (backCount == BACK_MAX) {   // いっぱいなら、いちばん古いものを捨てる
        for (int i = 1; i < BACK_MAX; i++) backStack[i - 1] = backStack[i];
        backCount--;
    }
    backStack[backCount++] = front;
    switchTo(app);
    printStack();
    markChanged();
}

void goBack() {
    UiApp* app = backCount > 0 ? backStack[--backCount] : &homeApp;   // 空ならホームへ
    switchTo(app);
    if (app->resume) app->resume();
    printStack();
    markChanged();
}

// loop() は前面のアプリの key()・draw() を呼ぶだけ（前面でないアプリには、キーも画面も渡さない）
void loop() {
    static unsigned long drawLast = 0;

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        front->key(M5Cardputer.Keyboard.keysState());
        redrawNow = true;
        markChanged();
    }

    // 最後の変化から3秒たったら /.last を書く
    if (lastDirty && millis() - changedAt >= 3000) {
        writeLast();
        lastDirty = false;
    }

    // 打ったらすぐ、打たなくてもカーソルの点滅のために 250ms ごとに描く
    unsigned long now = millis();
    if (redrawNow || now - drawLast >= 250) {
        redrawNow = false;
        drawLast = now;
        front->draw(shellCanvas);
        shellCanvas.pushSprite(0, 0);
    }
    delay(10);  // 10ms 休んで、ほかのタスクに順番をゆずる
}
