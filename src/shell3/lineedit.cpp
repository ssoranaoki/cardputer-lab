// 1行の編集を助ける部品: 履歴（fn＋; / fn＋.）と補完（Tab。命令・仕事・ファイルの名前）
// どちらも「シェルのメモリの中」に覚えているだけ。電源を切ると消える（ファイルには書かない）
#include <M5Cardputer.h>
#include "shell.h"

// ---- 履歴: 打った命令を新しい順に覚えておく（シェルのメモリの中。電源を切ると消える） ----
constexpr int HIST_MAX = 16;
String hist[HIST_MAX];
int histCount = 0;                       // 覚えている数
int histPos = -1;                        // いま見ている履歴（-1 = 打ちかけの新しい行）
String draft;                            // 履歴をさかのぼる前に打ちかけていた行

// ---- 履歴 ----
void histPush(const String& line) {
    String s = line;
    s.trim();
    if (s.length() == 0) return;
    if (histCount > 0 && hist[histCount - 1] == s) return;   // 同じ命令が続いたら1つだけ覚える
    if (histCount == HIST_MAX) {
        for (int i = 1; i < HIST_MAX; i++) hist[i - 1] = hist[i];   // いちばん古いのを捨てる
        histCount--;
    }
    hist[histCount++] = s;
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
const char* COMMANDS[] = {"help", "ps", "run", "stop", "go", "kill", "free", "history", "ls", "cat", "echo", "rm", "mkdir", "cd", "pwd", "clear"};
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
    } else if ((cmd == "ls" || cmd == "cat" || cmd == "rm" || cmd == "cd") && rest.indexOf(' ') < 0) {
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

