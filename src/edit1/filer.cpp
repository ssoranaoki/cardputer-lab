// ファイラー（v0.9 単位2、案C の見た目）。フォルダの中を一覧で見て、fn＋; fn＋. で選び、Enter で入る
// 自分のフォルダ dir を持つ（シェルの cwd とは別。ファイラーで動いてもシェルのいる場所は変わらない）
#include <M5Cardputer.h>
#include <LittleFS.h>
#include "shell.h"
#include "uiapp.h"

namespace {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int TOP_H = 14;          // 上の帯（いまのフォルダ）
constexpr int ROW_H = 15;          // 1行の高さ
constexpr int ROWS = 7;            // 見える行の数
constexpr int BOTTOM_H = 12;       // 下の帯（キーの案内）
constexpr int MAX_ENTRIES = 64;

struct Entry {
    String name;
    bool isDir;
    bool isUp;                     // ".."（1つ上へ）
    size_t size;
};

String dir = "/";                  // ファイラーのいるフォルダ
Entry entries[MAX_ENTRIES];
int count = 0;
int sel = 0;                       // 選んでいる行
int top = 0;                       // 窓のいちばん上の行

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return M5Cardputer.Display.color565(r, g, b); }

// 並べる順: .. → フォルダ → ファイル、同じ種類の中は名前順
bool before(const Entry& a, const Entry& b) {
    if (a.isUp != b.isUp) return a.isUp;
    if (a.isDir != b.isDir) return a.isDir;
    return a.name.compareTo(b.name) < 0;
}

// フォルダの中身は LittleFS に聞いて毎回読む（ファイラーは名前を覚えていない）
void loadDir() {
    count = 0;
    sel = top = 0;
    if (dir != "/") entries[count++] = {"..", true, true, 0};
    if (!fsIsReady()) return;
    File d = LittleFS.open(dir);
    if (!d || !d.isDirectory()) return;
    File f = d.openNextFile();
    while (f && count < MAX_ENTRIES) {
        if (f.name()[0] != '.')          // . で始まる名前は隠す（ls と同じ）
            entries[count++] = {String(f.name()), f.isDirectory(), false, f.isDirectory() ? 0 : (size_t)f.size()};
        f = d.openNextFile();
    }
    // 64件までなので、そのまま差し込んで並べる（挿入ソート）
    for (int i = 1; i < count; i++) {
        Entry e = entries[i];
        int j = i - 1;
        while (j >= 0 && before(e, entries[j])) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = e;
    }
}

// 窓（top）を sel に付いていかせる（メモ帳と同じ）
void follow() {
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
}

void filesKey(const Keyboard_Class::KeysState& keys) {
    if (keys.esc) {
        goBack();
    } else if (keys.up) {
        if (sel > 0) sel--;              // 端で止まる
        follow();
    } else if (keys.down) {
        if (sel < count - 1) sel++;
        follow();
    } else if (keys.enter && count > 0) {
        const Entry& e = entries[sel];
        if (e.isUp) {                    // 1つ上へ: 最後の / より前
            int slash = dir.lastIndexOf('/');
            dir = slash <= 0 ? String("/") : dir.substring(0, slash);
            loadDir();
        } else if (e.isDir) {            // その中へ
            dir = (dir == "/" ? String("/") : dir + "/") + e.name;
            loadDir();
        } else {                         // ファイル: メモ帳で開く
            String p = (dir == "/" ? String("/") : dir + "/") + e.name;
            if (editOpen(p)) openApp(&memoApp);
        }
    }
}

// 行の左の小さな印。x, y は印の左上（12×11 くらい）
void drawIcon(M5Canvas& c, const Entry& e, int x, int y) {
    const uint16_t blue = rgb(0x7C, 0xC2, 0xEE);
    if (e.isUp) {                        // 上向きの矢印
        c.fillTriangle(x + 6, y, x, y + 6, x + 12, y + 6, blue);
        c.fillRect(x + 4, y + 6, 4, 5, blue);
    } else if (e.isDir) {                // フォルダ（つまみと本体）
        c.fillRoundRect(x, y + 1, 6, 4, 1, blue);
        c.fillRoundRect(x, y + 3, 12, 8, 2, blue);
    } else {                             // 紙（右上を折る）
        const uint16_t grey = rgb(0xA0, 0xA0, 0xA0);
        c.drawRect(x + 2, y, 9, 11, grey);
        c.drawLine(x + 7, y, x + 10, y + 3, grey);
        c.fillRect(x + 8, y, 3, 3, WHITE);
        c.drawLine(x + 7, y, x + 7, y + 3, grey);
        c.drawLine(x + 7, y + 3, x + 10, y + 3, grey);
    }
}

void filesDraw(M5Canvas& c) {
    const uint16_t grey = rgb(0x80, 0x80, 0x80);
    const uint16_t pale = rgb(0xEE, 0xF3, 0xF7);
    c.fillSprite(WHITE);

    // 上の帯: 真ん中にいまのフォルダ
    c.fillRect(0, 0, SCREEN_W, TOP_H, rgb(0x7C, 0xC2, 0xEE));
    c.setFont(&fonts::Font0);
    c.setTextColor(WHITE);
    c.setTextDatum(middle_center);
    c.drawString(dir, SCREEN_W / 2, TOP_H / 2);

    // 一覧: top 行目から7行
    c.setFont(&fonts::Font2);
    for (int i = 0; i < ROWS; i++) {
        int k = top + i;
        if (k >= count) break;
        const Entry& e = entries[k];
        int y = TOP_H + i * ROW_H;
        if (k == sel) c.fillRect(0, y, SCREEN_W, ROW_H, rgb(0xDC, 0xEF, 0xFF));
        drawIcon(c, e, 6, y + 2);

        String name = e.name;            // 長い名前は「大きさ」の手前で切る
        if (c.textWidth(name) > 160) {
            while (name.length() > 1 && c.textWidth(name + "~") > 160) name.remove(name.length() - 1);
            name += "~";
        }
        c.setTextColor(grey);
        c.setTextDatum(middle_left);
        c.drawString(name, 24, y + ROW_H / 2);

        c.setTextDatum(middle_right);
        if (e.isUp) {
        } else if (e.isDir) {
            c.drawString("DIR", SCREEN_W - 6, y + ROW_H / 2);
        } else {
            c.drawString(String((unsigned)e.size), SCREEN_W - 6, y + ROW_H / 2);
        }
    }
    if (count == 0) {
        c.setTextColor(grey);
        c.setTextDatum(middle_center);
        c.drawString(fsIsReady() ? "(empty)" : "no file system", SCREEN_W / 2, TOP_H + ROW_H * 2);
    }

    // 下の帯: キーの案内
    c.setFont(&fonts::Font0);
    c.fillRect(0, SCREEN_H - BOTTOM_H, SCREEN_W, BOTTOM_H, pale);
    c.setTextColor(grey);
    c.setTextDatum(middle_center);
    c.drawString("Enter open Esc home", SCREEN_W / 2, SCREEN_H - BOTTOM_H / 2);
    c.setTextDatum(top_left);            // シェルとメモ帳は左上ぞろえで描くので戻しておく
}

// メモ帳から戻ってきたとき: 読み直して（保存した大きさがすぐ変わる）、前に選んでいた名前の行を選び直す
void filesResume() {
    String name = sel < count ? entries[sel].name : String();
    loadDir();                           // sel は 0 に戻る
    for (int i = 0; i < count; i++) {
        if (entries[i].name == name) {
            sel = i;
            break;
        }
    }
    follow();
}

// いまの場所: "files <dir> <選んでいる行の名前>"（.. や空のときは名前なし）
String filesSave() {
    String s = String("files ") + dir;
    if (sel < count && !entries[sel].isUp) s += " " + entries[sel].name;
    return s;
}

// filesSave の行から戻す: arg は "<dir> <名前>"（名前はなくてよい）。フォルダが消えていたら戻さない
bool filesRestore(const String& arg) {
    String folder = arg, name;
    int sp = arg.indexOf(' ');
    if (sp >= 0) {
        folder = arg.substring(0, sp);
        name = arg.substring(sp + 1);
    }
    if (!folderExists(folder)) return false;
    dir = folder;
    loadDir();                           // sel は 0 に戻る
    for (int i = 0; i < count; i++) {
        if (name.length() > 0 && entries[i].name == name) {
            sel = i;
            break;
        }
    }
    follow();
    return true;
}

}  // namespace

UiApp filesApp = {"files", filesKey, filesDraw, filesResume, filesSave, filesRestore};

// ホームの Files から呼ぶ。開くたびに読み直すので、シェルで作ったファイルもすぐ出る
void filesShow() {
    loadDir();
    openApp(&filesApp);
}
