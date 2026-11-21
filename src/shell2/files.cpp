// ファイルの命令: ls・cat（v0.5 単位2）
//
// ファイルの置き場所は、内蔵フラッシュ（8MB）の一部。arduino-esp32 2.0.16 の default_8MB.csv にある
// 「spiffs」という名前の区画（0x670000 から 1.5MB）を、LittleFS というファイルシステムで使う。
// LittleFS.begin() は、この区画にファイルの帳面（どの名前のファイルが、フラッシュのどこにあるか）を作って管理する。
// シェルは「このファイルを開いて」と頼むだけで、フラッシュのどこを読むかは LittleFS が決める。
//
// 関数の形は arduino-esp32 2.0.16 の libraries/FS/src/FS.h・libraries/LittleFS/src/LittleFS.h で確認した:
//   LittleFS.begin(formatOnFail, basePath, maxOpenFiles, partitionLabel="spiffs")
//   File::isDirectory()・openNextFile()・name()（名前だけ）・size()、FS::open()・exists()・mkdir()

#include <M5Cardputer.h>
#include <LittleFS.h>
#include "shell.h"

static bool fsReady = false;

// 初めて起動したとき、ファイルがなければ見本を作る（microSD がないので、自分で置いておく）
static void writeIfMissing(const char* path, const char* text) {
    if (LittleFS.exists(path)) return;
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) return;
    f.print(text);
    f.close();
}

void fsInit() {
    // true: 区画にまだファイルシステムがないとき（最初の1回）は、まっさらに作る（フォーマット）
    fsReady = LittleFS.begin(true);
    if (!fsReady) {
        printLine("LittleFS: could not mount", RED);
        return;
    }
    writeIfMissing("/hello.txt", "Hello from LittleFS!\nThis file lives in the flash of the Cardputer.\n");
    if (!LittleFS.exists("/notes")) LittleFS.mkdir("/notes");
    writeIfMissing("/notes/todo.txt",
                   "1. history and Tab  (done)\n"
                   "2. ls and cat\n"
                   "3. write files next time\n");
}

// "notes" も "/notes" も "/notes/" も、同じ "/notes" にそろえる
static String normPath(const String& arg) {
    String p = arg;
    p.trim();
    if (!p.startsWith("/")) p = "/" + p;
    while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
    return p;
}

void cmdLs(const String& arg) {
    if (!fsReady) {
        printLine("no file system", RED);
        return;
    }
    String path = normPath(arg.length() ? arg : "/");
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) {
        printLine(String("no such folder: ") + path, RED);
        return;
    }
    int count = 0;
    File f = dir.openNextFile();
    while (f) {
        char buf[48];
        if (f.isDirectory()) {
            snprintf(buf, sizeof(buf), "%-20s <DIR>", (String(f.name()) + "/").c_str());
            printLine(buf, CYAN);
        } else {
            snprintf(buf, sizeof(buf), "%-20s %6u", f.name(), (unsigned)f.size());
            printLine(buf, WHITE);
        }
        count++;
        f = dir.openNextFile();
    }
    if (count == 0) printLine("(empty)", DARKGREY);
}

void cmdCat(const String& arg) {
    if (!fsReady) {
        printLine("no file system", RED);
        return;
    }
    if (arg.length() == 0) {
        printLine("usage: cat <file>", RED);
        return;
    }
    String path = normPath(arg);
    File f = LittleFS.open(path);
    if (!f || f.isDirectory()) {
        printLine(String("no such file: ") + path, RED);
        return;
    }
    constexpr int MAX_LINES = 60;   // 長いファイルは先頭だけ（プレゼンの決まり）
    int lines = 0;
    while (f.available() && lines < MAX_LINES) {
        String line = f.readStringUntil('\n');
        line.replace("\r", "");
        printLine(line, WHITE);
        lines++;
    }
    if (f.available()) printLine("... (first 60 lines)", DARKGREY);
    f.close();
}

// Tab の補完用: フォルダ dir（"/" や "/notes"）の中の名前を集める。フォルダの名前には / を付ける
int fileNames(const String& dir, String* out, int max) {
    if (!fsReady) return 0;
    File d = LittleFS.open(normPath(dir.length() ? dir : "/"));
    if (!d || !d.isDirectory()) return 0;
    int n = 0;
    File f = d.openNextFile();
    while (f && n < max) {
        out[n++] = String(f.name()) + (f.isDirectory() ? "/" : "");
        f = d.openNextFile();
    }
    return n;
}
