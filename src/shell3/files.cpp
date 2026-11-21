// ファイルの命令: ls・cat（v0.5 単位2）、echo（> と >>）・rm（v0.6 単位1）、mkdir・cd・pwd（v0.6 単位2）
//
// いまいるフォルダ cwd はシェルのメモリの中の変数（電源を切ると / に戻る）。フラッシュの中は何も変わらない。
// 名前は resolvePath() で「いまいるフォルダから数えた、/ で始まる名前」に直してから LittleFS に渡す
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

static String cwd = "/";          // いまいるフォルダ（シェルのメモリの中）

const String& currentDir() { return cwd; }

// 名前を、いまいるフォルダから数えた「/ で始まる名前」に直す。
// "todo.txt"（cwd が /notes）→ "/notes/todo.txt"、"/hello.txt" はそのまま、".." は1つ上、"." はそのまま
static String resolvePath(const String& arg) {
    String p = arg;
    p.trim();
    String full = p.startsWith("/") ? p : (cwd == "/" ? "/" + p : cwd + "/" + p);
    // "/" で区切って、"." を飛ばし、".." で1つ戻る
    String parts[16];
    int n = 0;
    int start = 1;
    while (start <= (int)full.length()) {
        int slash = full.indexOf('/', start);
        if (slash < 0) slash = full.length();
        String part = full.substring(start, slash);
        if (part == "..") {
            if (n > 0) n--;
        } else if (part.length() > 0 && part != "." && n < 16) {
            parts[n++] = part;
        }
        start = slash + 1;
    }
    String out;
    for (int i = 0; i < n; i++) out += "/" + parts[i];
    return out.length() ? out : "/";
}

static bool isFolder(const String& path) {
    File d = LittleFS.open(path);
    bool yes = d && d.isDirectory();
    if (d) d.close();
    return yes;
}

void cmdPwd() {
    printLine(cwd, CYAN);
}

// cd <フォルダ>: いまいるフォルダを変える。cd だけなら / に戻る。変わるのはシェルの変数 cwd だけ
void cmdCd(const String& arg) {
    String path = arg.length() ? resolvePath(arg) : String("/");
    if (!fsReady || !isFolder(path)) {
        printLine(String("no such folder: ") + path, RED);
        return;
    }
    cwd = path;
}

void cmdMkdir(const String& arg) {
    if (!fsReady) {
        printLine("no file system", RED);
        return;
    }
    if (arg.length() == 0) {
        printLine("usage: mkdir <folder>", RED);
        return;
    }
    String path = resolvePath(arg);
    if (LittleFS.exists(path)) {
        printLine(String("already exists: ") + path, YELLOW);
        return;
    }
    if (LittleFS.mkdir(path)) {
        printLine(String("made ") + path, GREEN);
    } else {
        printLine(String("could not make: ") + path, RED);
    }
}

void cmdLs(const String& arg) {
    if (!fsReady) {
        printLine("no file system", RED);
        return;
    }
    String path = arg.length() ? resolvePath(arg) : cwd;
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
    String path = resolvePath(arg);
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

// Tab の補完用: フォルダ dir（空ならいまいるフォルダ、"notes/" などはいまいるフォルダから数える）の中の名前を集める。フォルダの名前には / を付ける
int fileNames(const String& dir, String* out, int max) {
    if (!fsReady) return 0;
    File d = LittleFS.open(dir.length() ? resolvePath(dir) : cwd);
    if (!d || !d.isDirectory()) return 0;
    int n = 0;
    File f = d.openNextFile();
    while (f && n < max) {
        out[n++] = String(f.name()) + (f.isDirectory() ? "/" : "");
        f = d.openNextFile();
    }
    return n;
}

// echo <言葉> > <ファイル>  : ファイルを作り直して1行書く（前の中身は消える。モード "w"）
// echo <言葉> >> <ファイル> : ファイルの後ろに1行足す（なければ作る。モード "a"）
// echo <言葉>               : 言葉をそのまま画面に出す
void cmdEcho(const String& arg) {
    int app = arg.lastIndexOf(" >> ");
    int wr = arg.lastIndexOf(" > ");
    bool append = app >= 0 && app >= wr - 1;     // " >> " のほうが後ろにあれば足す
    int at = append ? app : wr;
    if (at < 0) {
        printLine(arg, WHITE);
        return;
    }
    if (!fsReady) {
        printLine("no file system", RED);
        return;
    }
    String text = arg.substring(0, at);
    String name = arg.substring(at + (append ? 4 : 3));
    name.trim();
    if (name.length() == 0) {
        printLine("usage: echo <text> > <file>", RED);
        return;
    }
    String path = resolvePath(name);
    if (isFolder(path)) {
        printLine(String("is a folder: ") + path, RED);
        return;
    }
    File f = LittleFS.open(path, append ? FILE_APPEND : FILE_WRITE);
    if (!f) {
        printLine(String("could not write: ") + path, RED);
        return;
    }
    f.print(text + "\n");
    f.close();
    printLine(String(append ? "appended " : "wrote ") + path, GREEN);
}

// rm <ファイル>: ファイルを消す。フォルダは消さない（まちがえて中身ごと消さないように）
void cmdRm(const String& arg) {
    if (!fsReady) {
        printLine("no file system", RED);
        return;
    }
    if (arg.length() == 0) {
        printLine("usage: rm <file>", RED);
        return;
    }
    String path = resolvePath(arg);
    File f = LittleFS.open(path);
    if (!f) {
        printLine(String("no such file: ") + path, RED);
        return;
    }
    bool isDir = f.isDirectory();
    f.close();
    if (isDir) {
        printLine(String("is a folder (not removed): ") + path, RED);
        return;
    }
    if (LittleFS.remove(path)) {
        printLine(String("removed ") + path, GREEN);
    } else {
        printLine(String("could not remove: ") + path, RED);
    }
}

