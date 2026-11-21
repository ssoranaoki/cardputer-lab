// メモ帳（edit <ファイル>）v0.7
// 単位1: 開いて見る。ファイルを RAM のバッファ（行の表 buf[]）に読み込み、11行の窓に映す。fn＋矢印で動く。Esc（fn＋`）でシェルへ
// 単位2: 文字を入れる・Enter で行を分ける・⌫ で消す／つなぐ
// 単位3: Ctrl+S で保存（FILE_WRITE で全部の行を書き直す）・* 印・Esc の確認
//
// しくみ:
//   フラッシュのファイル → 開くときに1行ずつ読む → RAM のバッファ buf[]（直すのはこの写し）→ 窓の11行だけ描く → 画面
//   カーソルは「バッファの中のいまの場所」: 行の番号 row と、行の中の文字の番号 col
//   スクロールは「窓がバッファのどこを見ているか」: 縦は top（窓のいちばん上の行）、横は left（窓のいちばん左の文字）
// キー（M5Cardputer bf64d31 の Keyboard.h）: fn＋; ↑・fn＋. ↓・fn＋, ←・fn＋/ →・fn＋` Esc
#include <M5Cardputer.h>
#include <LittleFS.h>
#include "shell.h"
#include "uiapp.h"

namespace {

constexpr int MAX_LINES = 200;    // バッファに入れる行の数の上限（RAM を食べすぎないように）
constexpr int COLS = 40;          // 1行に見える文字の数（Font0 は 6 ドット × 40 = 240）
constexpr int LINE_H = 10;
constexpr int TEXT_ROWS = 11;     // 窓の行の数（1行目は名前の帯、最後の行はキーの案内）

String buf[MAX_LINES];            // バッファ: ファイルの中身の写し（1つの箱に1行。改行は入れない）
int lineCount = 0;                // バッファの行の数（いつも1以上）
int row = 0, col = 0;             // カーソル
int top = 0, left = 0;            // 窓の位置（スクロール）
String path;                      // 開いているファイルの名前（/ から）
bool active = false;              // メモ帳が開いているか
bool isNew = false;               // まだないファイルを開いた（保存すると作る）
bool modified = false;            // 開いてから（または保存してから）直したか
bool escArmed = false;            // 直したまま Esc を1回押した（もう1回で、保存せずに閉じる）
String msg;                       // 知らせ（最後の行に、キーの案内の代わりに出す）
uint16_t msgColor = YELLOW;
unsigned long msgUntil = 0;       // 知らせを消す時刻（millis）

// ファイルを1行ずつバッファに読み込む。ファイルの最後の改行のあとには行を作らない
void loadFile(File& f) {
    lineCount = 0;
    while (f.available() && lineCount < MAX_LINES) {
        String line = f.readStringUntil('\n');
        line.replace("\r", "");
        buf[lineCount++] = line;
    }

    if (lineCount == 0) buf[lineCount++] = "";   // 空のファイルでも、カーソルを置く行を1つ作る
}

// カーソルが窓の外に出たら、窓（top・left）を動かして付いていく
void follow() {
    if (row < top) top = row;
    if (row >= top + TEXT_ROWS) top = row - TEXT_ROWS + 1;
    if (col < left) left = col;
    if (col >= left + COLS - 1) left = col - (COLS - 2);   // 右端の1文字ぶんはカーソルのために空けておく
}

void moveUp() {
    if (row == 0) return;
    row--;
    if (col > (int)buf[row].length()) col = buf[row].length();   // 短い行に移ったら、行の終わりに寄せる
}

void moveDown() {
    if (row >= lineCount - 1) return;
    row++;
    if (col > (int)buf[row].length()) col = buf[row].length();
}

void moveLeft() {
    if (col > 0) {
        col--;
    } else if (row > 0) {   // 行の頭で ← なら、前の行の終わりへ
        row--;
        col = buf[row].length();
    }
}

void moveRight() {
    if (col < (int)buf[row].length()) {
        col++;
    } else if (row < lineCount - 1) {   // 行の終わりで → なら、次の行の頭へ
        row++;
        col = 0;
    }
}

// 知らせを2秒だけ出す
void notify(const String& text, uint16_t color) {
    msg = text;
    msgColor = color;
    msgUntil = millis() + 2000;
}

// カーソルの所に1文字入れる。行をカーソルの前と後ろに分けて、間に c をはさむ
void insertChar(char c) {
    buf[row] = buf[row].substring(0, col) + c + buf[row].substring(col);
    col++;
    modified = true;
}

// Enter: 行をカーソルの所で2つに分ける。後ろの半分は次の行になり、カーソルはその頭へ
void splitLine() {
    if (lineCount >= MAX_LINES) return;   // バッファがいっぱいなら分けない
    for (int i = lineCount; i > row + 1; i--) buf[i] = buf[i - 1];   // 下の行を後ろから1つずつ下へずらして、場所を空ける
    buf[row + 1] = buf[row].substring(col);
    buf[row] = buf[row].substring(0, col);
    lineCount++;
    row++;
    col = 0;
    modified = true;
}

// ⌫: カーソルの前の1文字を消す。行の頭なら、今の行を前の行の後ろにつなぐ
void backspace() {
    if (col > 0) {
        buf[row].remove(col - 1, 1);
        col--;
        modified = true;
    } else if (row > 0) {
        col = buf[row - 1].length();   // カーソルは、つなぐ前の「前の行の終わり」へ
        buf[row - 1] += buf[row];
        for (int i = row; i < lineCount - 1; i++) buf[i] = buf[i + 1];   // 下の行を1つずつ上へずらして、空いた行をつめる
        lineCount--;
        row--;
        modified = true;
    }
    // 1行目の頭では何も変わらないので、modified はそのまま
}

// Ctrl+S: バッファの全部の行をファイルに書き直す（FILE_WRITE は前の中身を消してから書く）。
// 最後の行のあとにも改行を書く。読むときは最後の改行のあとに行を作らないので、開き直しても同じ行数になる
bool saveFile() {
    File f = LittleFS.open(path, FILE_WRITE);   // ないファイルなら作る
    if (!f) {
        notify("could not write", YELLOW);
        return false;
    }
    for (int i = 0; i < lineCount; i++) {
        f.print(buf[i]);
        f.print('\n');
    }
    f.close();
    modified = false;
    isNew = false;
    Serial.printf("saved %s: %d lines\n", path.c_str(), lineCount);
    notify("saved", GREEN);
    return true;
}

}  // namespace

bool editorActive() { return active; }

bool editOpen(const String& arg) {
    if (!fsIsReady()) {
        printLine("no file system", RED);
        return false;
    }
    if (arg.length() == 0) {
        printLine("usage: edit <file>", RED);
        return false;
    }
    String p = fullPath(arg);
    if (folderExists(p)) {
        printLine(String("is a folder: ") + p, RED);
        return false;
    }
    File f = LittleFS.open(p);   // 読むだけ（FILE_READ）。ないファイルなら開けない
    if (f) {
        loadFile(f);
        f.close();
        isNew = false;
    } else {
        lineCount = 0;
        buf[lineCount++] = "";
        isNew = true;
    }
    path = p;
    row = col = top = left = 0;
    modified = escArmed = false;
    msgUntil = 0;
    active = true;
    Serial.printf("edit %s: %d lines%s\n", path.c_str(), lineCount, isNew ? " (new file)" : "");
    return true;
}

void editorKey(const Keyboard_Class::KeysState& keys) {
    // Ctrl+S（Ctrl を押していると word には大文字の 'S' が入る）
    if (keys.ctrl) {
        for (char c : keys.word) {
            if (c == 's' || c == 'S') {
                escArmed = false;
                saveFile();
                return;
            }
        }
    }
    if (keys.esc) {
        if (!modified) {
            active = false;
            printLine(String("closed ") + path, GREEN);
        } else if (escArmed) {   // 2回目の Esc: 直したものを捨てて閉じる
            active = false;
            printLine(String("closed ") + path + " (not saved)", YELLOW);
        } else {                 // 1回目の Esc: 閉じずに知らせる
            escArmed = true;
            notify("unsaved! Esc again to discard", YELLOW);
        }
        return;
    }
    // 本当の操作のキーが来たら、確認は取り消し。fn などだけが変わったときは取り消さない
    // （Esc は fn＋` なので、` を離したときに fn だけの変化が来る）
    if (keys.up || keys.down || keys.left || keys.right || keys.enter ||
        keys.backspace || keys.del || keys.word.size() > 0) {
        escArmed = false;
    }
    if (keys.up) moveUp();
    else if (keys.down) moveDown();
    else if (keys.left) moveLeft();
    else if (keys.right) moveRight();
    else if (keys.enter) splitLine();
    else if (keys.backspace || keys.del) backspace();
    else if (!keys.ctrl && keys.word.size() > 0) {   // Ctrl を押しているときの文字は入れない（Tab も入れない）
        for (char c : keys.word) insertChar(c);
    }
    follow();
}

void editorDraw(M5Canvas& canvas) {
    canvas.fillSprite(BLACK);
    canvas.setFont(&fonts::Font0);

    // 1行目: 名前の帯（ファイルの名前・新しいファイルか・カーソルの位置。L と C は 1 から数える）
    canvas.fillRect(0, 0, 240, LINE_H, 0x0A2A);   // 濃い青緑
    canvas.setTextColor(CYAN);
    String name = path + (modified ? " *" : "") + (isNew ? " (new)" : "");   // * は直したまま保存していない印
    if ((int)name.length() > 26) name = "..." + name.substring(name.length() - 23);
    canvas.drawString(name, 2, 1);
    String pos = String("L") + (row + 1) + " C" + (col + 1);
    canvas.drawString(pos, 240 - 2 - pos.length() * 6, 1);

    // 窓の11行: バッファの top 行目から。横は left 文字目から 40 文字
    for (int i = 0; i < TEXT_ROWS; i++) {
        int r = top + i;
        int y = LINE_H + 2 + i * LINE_H;
        if (r >= lineCount) {
            canvas.setTextColor(DARKGREY);
            canvas.drawString("~", 0, y);   // バッファの行がない所（vi と同じ印）
            continue;
        }
        String shown = (int)buf[r].length() > left ? buf[r].substring(left, left + COLS) : String("");
        canvas.setTextColor(WHITE);
        canvas.drawString(shown, 0, y);
        // カーソル（0.5秒ごとに点滅）: 四角を塗って、その上に文字を黒で描く
        if (r == row && (millis() / 500) % 2 == 0) {
            int x = (col - left) * 6;
            canvas.fillRect(x, y, 6, 8, GREEN);
            if (col < (int)buf[r].length()) {
                canvas.setTextColor(BLACK);
                canvas.drawString(String(buf[r][col]), x, y);
            }
        }
    }

    // 最後の行: 知らせ（出してから2秒）か、キーの案内
    if (millis() < msgUntil) {
        canvas.setTextColor(msgColor);
        canvas.drawString(msg, 2, 135 - LINE_H + 1);
    } else {
        canvas.setTextColor(DARKGREY);
        canvas.drawString("^S save Esc quit fn+; , . / move", 2, 135 - LINE_H + 1);
    }
}

// ---- メモ帳のアプリ（v0.9 単位3）: シェルの中に重ねず、1つのアプリとして前面に出す ----
namespace {
void memoKey(const Keyboard_Class::KeysState& keys) {
    editorKey(keys);
    if (!editorActive()) goBack();   // Esc で閉じたら、開いたところ（シェル・ファイラー・ホーム）に戻る
}

// いまの場所: "memo <path> <カーソルの行（1から）>"
String memoSave() {
    return String("memo ") + path + " " + (row + 1);
}

// memoSave の行から戻す: arg は "<path> <行>"。消えたファイルは空で開かない
bool memoRestore(const String& arg) {
    String p = arg;
    int line = 1;
    int sp = arg.lastIndexOf(' ');
    if (sp >= 0) {
        p = arg.substring(0, sp);
        line = arg.substring(sp + 1).toInt();
    }
    if (!fsIsReady() || !LittleFS.exists(p)) return false;
    if (!editOpen(p)) return false;
    row = min(max(line - 1, 0), lineCount - 1);
    col = 0;
    follow();
    return true;
}
}  // namespace

UiApp memoApp = {"memo", memoKey, editorDraw, nullptr, memoSave, memoRestore};
