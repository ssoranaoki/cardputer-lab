// 1行の編集を助ける部品: 履歴（fn＋; / fn＋.）と補完（Tab。命令・仕事・ファイルの名前）
// 履歴は RAM の表に加えて /.history にも書く（v0.8）。書き足し FILE_APPEND と、長くなったら書き直す FILE_WRITE を使い分ける。
// 電源スイッチを切る瞬間には知らせが来ないので、命令のたびに書く。補完はメモリの中だけ
#include <M5Cardputer.h>
#include <LittleFS.h>
#include "shell.h"

// ---- 履歴: 打った命令を新しい順に覚えておく（RAM の表。起動のときに /.history から読み直す） ----
constexpr int HIST_MAX = 16;
String hist[HIST_MAX];
int histCount = 0;                       // 覚えている数
int histPos = -1;                        // いま見ている履歴（-1 = 打ちかけの新しい行）
String draft;                            // 履歴をさかのぼる前に打ちかけていた行

constexpr const char* HIST_FILE = "/.history";
constexpr int HIST_FILE_MAX = 64;        // ファイルの行がこれをこえたら、表の16件だけに書き直す
int fileLines = 0;                       // ファイルの行の数

// ---- 履歴 ----
// RAM の表に足す。足したら true、空・同じ命令の続きなら false
static bool addToTable(const String& line) {
    String s = line;
    s.trim();
    if (s.length() == 0) return false;
    if (histCount > 0 && hist[histCount - 1] == s) return false;   // 同じ命令が続いたら1つだけ覚える
    if (histCount == HIST_MAX) {
        for (int i = 1; i < HIST_MAX; i++) hist[i - 1] = hist[i];   // いちばん古いのを捨てる
        histCount--;
    }
    hist[histCount++] = s;
    return true;
}

// 起動のとき: /.history を1行ずつ読んで表に入れる（16件をこえたら古いのが押し出され、最後の16件が残る）
void histLoad() {
    if (!fsIsReady()) return;
    fileLines = 0;
    File f = LittleFS.open(HIST_FILE);
    if (f && !f.isDirectory()) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            line.trim();
            fileLines++;
            if (line.length()) addToTable(line);
        }
    }
    if (f) f.close();
    Serial.printf("history: loaded %d from /.history (%d lines)\n", histCount, fileLines);
}

// 表に足せたら、ファイルにも書く。ふだんは後ろに1行足すだけ（速い・消える心配が少ない）
void histPush(const String& line) {
    if (!addToTable(line)) return;
    if (!fsIsReady()) return;
    File f = LittleFS.open(HIST_FILE, FILE_APPEND);
    if (!f) return;
    f.print(hist[histCount - 1] + "\n");
    f.close();
    fileLines++;
    if (fileLines > HIST_FILE_MAX) {     // 長くなったら、表の分だけに書き直す
        f = LittleFS.open(HIST_FILE, FILE_WRITE);
        if (!f) return;
        for (int i = 0; i < histCount; i++) f.print(hist[i] + "\n");
        f.close();
        fileLines = histCount;
        Serial.printf("history: compacted to %d lines\n", fileLines);
    }
}

void histUp() {      // fn＋;（↑）: 1つ古い命令へ
    if (histPos + 1 >= histCount) return;
    if (histPos == -1) draft = input;    // 打ちかけの行は取っておく
    histPos++;
    input = hist[histCount - 1 - histPos];
}

void histDown() {    // fn＋.（↓）: 1つ新しい命令へ。いちばん新しいの次は、打ちかけの行に戻る
    if (histPos < 0) return;
    histPos--;
    input = (histPos == -1) ? draft : hist[histCount - 1 - histPos];
}

void cmdHistory() {
    if (histCount == 0) {
        printLine("(no history)", DARKGREY);
        return;
    }
    for (int i = 0; i < histCount; i++) printLine(String(i + 1) + "  " + hist[i], CYAN);
}

// ---- 補完: Tab で、打ちかけの言葉の続きを補う ----
const char* COMMANDS[] = {"help", "ps", "run", "stop", "go", "kill", "free", "history", "ls", "cat", "echo", "rm", "mkdir", "cd", "pwd", "edit", "clear"};
constexpr int COMMAND_COUNT = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

bool takesAppName(const String& cmd) {
    return cmd == "run" || cmd == "stop" || cmd == "go" || cmd == "kill";
}

// 候補の中から、prefix で始まるものを集める。1つなら補い、2つ以上なら共通の部分まで補って一覧を出す
// 戻り値: 補ったあとの言葉
String completeWord(const String& prefix, const char* const* words, int n, bool addSpace) {
    String hits[16];
    int hitCount = 0;
    for (int i = 0; i < n && hitCount < 16; i++) {
        if (String(words[i]).startsWith(prefix)) hits[hitCount++] = words[i];
    }
    if (hitCount == 0) return prefix;                        // 候補なし: そのまま
    if (hitCount == 1) return hits[0] + (addSpace ? " " : "");
    // 2つ以上: みんなに共通する頭の部分まで補う
    String common = hits[0];
    for (int i = 1; i < hitCount; i++) {
        unsigned int k = 0;
        while (k < common.length() && k < hits[i].length() && common[k] == hits[i][k]) k++;
        common = common.substring(0, k);
    }
    String list;
    for (int i = 0; i < hitCount; i++) list += hits[i] + "  ";
    printLine(currentDir() + "> " + input, GREEN);
    printLine(list, DARKGREY);
    return common;
}

void complete() {
    int sp = input.indexOf(' ');
    if (sp < 0) {
        // 1つ目の言葉: 命令の名前を補う
        input = completeWord(input, COMMANDS, COMMAND_COUNT, true);
        return;
    }
    String cmd = input.substring(0, sp);
    String rest = input.substring(sp + 1);
    if (takesAppName(cmd) && rest.indexOf(' ') < 0) {
        // 2つ目の言葉: 仕事の名前を補う
        const char* names[16];
        int n = appCount();
        for (int i = 0; i < n && i < 16; i++) names[i] = appName(i);
        input = cmd + " " + completeWord(rest, names, n, false);
    } else if ((cmd == "ls" || cmd == "cat" || cmd == "rm" || cmd == "cd" || cmd == "edit") && rest.indexOf(' ') < 0) {
        // 2つ目の言葉: ファイルとフォルダの名前を補う。"notes/to" なら、フォルダ notes の中から "to" で始まる名前を探す
        int slash = rest.lastIndexOf('/');
        String dir = slash >= 0 ? rest.substring(0, slash + 1) : "";
        String part = slash >= 0 ? rest.substring(slash + 1) : rest;
        String found[16];
        int n = fileNames(dir, found, 16);   // dir が空なら、いまいるフォルダの中
        const char* names[16];
        for (int i = 0; i < n; i++) names[i] = found[i].c_str();
        // フォルダ（/ で終わる）は続けて中を補えるように空白を付けない。ファイルも最後の言葉なので付けない
        input = cmd + " " + dir + completeWord(part, names, n, false);
    }
}

