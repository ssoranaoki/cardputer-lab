// 日本語入力の試し打ち「Kana」（v1.1 単位1、案C の見た目。単位2 で ▽▼ と候補の帯。単位3 で送りがなと Enter で /kana.txt に書き足す）。変換は ime.cpp、ここは画面とキーだけ
#include <M5Cardputer.h>
#include <LittleFS.h>
#include "ime.h"
#include "shell.h"
#include "uiapp.h"

namespace {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int TOP_H = 16;       // 上の帯
constexpr int BOTTOM_H = 12;    // 下の帯
constexpr int TEXT_X = 4;
constexpr int TEXT_W = 232;     // 折り返す幅
constexpr int LINE_H = 15;
constexpr int BODY_Y = TOP_H + 3;
constexpr int BODY_ROWS = (SCREEN_H - BOTTOM_H - BODY_Y) / LINE_H;
constexpr int MAX_LINES = 64;
constexpr int CAND_H = 16;      // 候補の一覧の帯（下の帯の上）
constexpr uint32_t NOT_FOUND_MS = 2000;

Ime ime;
// 0 と区別するために | 1 すると、今より先の時刻になって引き算が負になることがあったので、出しているかは別の印で持つ
bool noticeOn = false;          // 帯に知らせを出しているか
uint32_t noticeAt = 0;          // 帯の知らせを出し始めた時刻
const char* notice = "";        // 帯に出す知らせ（「辞書にありません」「/kana.txt に書きました」）

void showNotice(const char* s) {
    notice = s;
    noticeOn = true;
    noticeAt = millis();
}

// 確定した text を /kana.txt の後ろに1行足して、text を空にする
void writeText() {
    if (!fsIsReady()) {
        showNotice("ファイルが使えません");
        return;
    }
    File f = LittleFS.open("/kana.txt", FILE_APPEND);
    if (!f) {
        showNotice("/kana.txt に書けません");
        return;
    }
    f.print(ime.text + "\n");
    f.close();
    ime.text = "";
    showNotice("/kana.txt に書きました");
}

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return M5Cardputer.Display.color565(r, g, b); }

// UTF-8 の1文字のバイト数（先頭のバイトから）
int charLen(uint8_t b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

void kanaKey(const Keyboard_Class::KeysState& keys) {
    if (keys.esc) {
        if (!ime.imeEscape()) goBack();
    } else if (keys.tab) {
        ime.imeToggle();
    } else if (keys.backspace || keys.del) {
        ime.imeBackspace();
    } else if (keys.enter) {   // 変換を決めるだけ（mode 0 では何もしない）
        ime.imeEnter();
    } else if (keys.ctrl) {    // Ctrl+S で /kana.txt に書く。Ctrl を押しているときの文字は入れない
        bool save = false;
        for (char c : keys.word) {
            if (c == 's' || c == 'S') save = true;
        }
        if (save) {
            ime.imeEnter();    // ▽・▼ の途中なら先に決める
            if (ime.text.length() > 0) {
                writeText();
            } else {
                showNotice("書くものがありません");
            }
        }
    } else {
        for (char c : keys.word) {   // Space も ' ' として word に入る
            if (c == ' ') {
                ime.imeSpace();
            } else {
                ime.imeKey(c);
            }
        }
    }
    if (ime.notFound) {
        ime.notFound = false;
        showNotice("辞書にありません");
    }
}

// ⌫ は efontJA_12 に無いので、線で描く（幅 12）
void drawBackspaceIcon(M5Canvas& c, int x, int cy, uint16_t col) {
    c.drawLine(x, cy, x + 3, cy - 4, col);
    c.drawLine(x, cy, x + 3, cy + 4, col);
    c.drawLine(x + 3, cy - 4, x + 10, cy - 4, col);
    c.drawLine(x + 3, cy + 4, x + 10, cy + 4, col);
    c.drawLine(x + 10, cy - 4, x + 10, cy + 4, col);
    c.drawLine(x + 5, cy - 2, x + 8, cy + 2, col);
    c.drawLine(x + 5, cy + 2, x + 8, cy - 2, col);
}

// 案内の文字: 英数字は Font0、日本語と記号は efontJA_12、⌫ は線で。middle_left で左から描き、右の端を返す
int drawGuide(M5Canvas& c, const char* s, int x, int cy, uint16_t col) {
    c.setTextColor(col);
    c.setTextDatum(middle_left);
    String str(s);
    for (int i = 0; i < (int)str.length();) {
        int n = charLen((uint8_t)str[i]);
        String ch = str.substring(i, i + n);
        if (ch == "⌫") {
            drawBackspaceIcon(c, x, cy, col);
            x += 12;
        } else {
            if (n == 1) {
                c.setFont(&fonts::Font0);
            } else {
                c.setFont(&fonts::efontJA_12);
            }
            c.drawString(ch, x, cy);
            x += c.textWidth(ch);
        }
        i += n;
    }
    return x;
}

// 候補の一覧（"1 漢字 2 幹事 …"）。入るだけ並べ、いまの候補が入らなければ次のページから
void drawCandidates(M5Canvas& c, int y) {
    c.setFont(&fonts::efontJA_12);
    c.setTextDatum(middle_left);
    int start = 0;
    int end = 0;
    for (;;) {
        int w = 0;
        end = start;
        while (end < ime.candCount) {
            String item = String(end + 1) + " " + ime.cands[end];
            int iw = c.textWidth(item) + 8;
            if (w + iw > TEXT_W && end > start) break;
            w += iw;
            end++;
        }
        if (ime.candIdx < end) break;
        start = end;
    }
    int x = TEXT_X;
    for (int i = start; i < end; i++) {
        String item = String(i + 1) + " " + ime.cands[i];
        c.setTextColor(i == ime.candIdx ? rgb(0x31, 0x1B, 0x92) : rgb(0xA0, 0x94, 0xB8));
        c.drawString(item, x, y + CAND_H / 2);
        x += c.textWidth(item) + 8;
    }
}

void kanaDraw(M5Canvas& c) {
    const uint16_t grey = rgb(0x80, 0x80, 0x80);
    const uint16_t lilac = rgb(0xB3, 0x9D, 0xDB);
    c.fillSprite(WHITE);

    // 上の帯: 左に名前、右にいまのモード
    c.fillRect(0, 0, SCREEN_W, TOP_H, lilac);
    c.setFont(&fonts::efontJA_12);
    c.setTextColor(WHITE);
    c.setTextDatum(middle_left);
    c.drawString("Kana", 4, TOP_H / 2);
    c.setTextDatum(middle_right);
    c.drawString(ime.kana ? "あ" : "A", SCREEN_W - 4, TOP_H / 2);

    // 候補の帯を出すか（▼ のとき・知らせの2秒）
    if (noticeOn && millis() - noticeAt >= NOT_FOUND_MS) noticeOn = false;
    bool candBand = ime.mode == 2 || noticeOn;
    int bodyRows = candBand ? (SCREEN_H - BOTTOM_H - CAND_H - BODY_Y) / LINE_H : BODY_ROWS;

    // 本文: text（黒）・変換中の部分（▽ の読み か ▼ の候補）・roma（灰）を続けて、文字の切れ目で 232 ドットに折り返す
    // 送りがなの途中は 読み*おくり、▼ は 候補＋送りがな
    String mid = ime.mode == 1   ? (ime.okuriHead.length() > 0 ? ime.yomi + "*" + ime.okuri : ime.yomi)
                 : ime.mode == 2 ? ime.cands[ime.candIdx] + ime.okuri
                                 : String();
    String all = ime.text + mid + ime.roma;
    int split1 = ime.text.length();          // ここから変換中の部分
    int split2 = split1 + mid.length();      // ここから roma
    int starts[MAX_LINES + 1];
    int lines = 1;
    starts[0] = 0;
    int w = 0;
    for (int i = 0; i < (int)all.length();) {
        int n = charLen((uint8_t)all[i]);
        int cw = c.textWidth(all.substring(i, i + n));
        if (w + cw > TEXT_W && w > 0) {
            if (lines == MAX_LINES) {   // 多すぎたら古い行を捨てる
                for (int k = 1; k < lines; k++) starts[k - 1] = starts[k];
                lines--;
            }
            starts[lines++] = i;
            w = 0;
        }
        w += cw;
        i += n;
    }
    // カーソルが入りきらなければ次の行に
    int cursorX = w;
    if (cursorX + 2 > TEXT_W && lines < MAX_LINES) {
        starts[lines++] = all.length();
        cursorX = 0;
    }
    starts[lines] = all.length();

    // 終わりの行が見えるように、下の bodyRows 行だけ描く
    int first = lines > bodyRows ? lines - bodyRows : 0;
    c.setTextDatum(top_left);
    for (int l = first; l < lines; l++) {
        int y = BODY_Y + (l - first) * LINE_H;
        int a = starts[l], b = starts[l + 1];
        int x = TEXT_X;
        if (a < split1) {
            String s = all.substring(a, min(b, split1));
            c.setTextColor(BLACK);
            c.drawString(s, x, y);
            x += c.textWidth(s);
        }
        if (a < split2 && b > split1) {
            String s = all.substring(max(a, split1), min(b, split2));
            int sw = c.textWidth(s);
            if (ime.mode == 2) {   // ▼ いまの候補: 薄紫の地に白
                c.fillRect(x, y - 1, sw, 14, rgb(0x7E, 0x57, 0xC2));
                c.setTextColor(WHITE);
            } else {               // ▽ 読み: 薄紫の下線
                c.fillRect(x, y + 12, sw, 2, lilac);
                c.setTextColor(BLACK);
            }
            c.drawString(s, x, y);
            x += sw;
        }
        if (b > split2) {
            String s = all.substring(max(a, split2), b);
            c.setTextColor(grey);
            c.drawString(s, x, y);
        }
        if (l == lines - 1 && (millis() / 500) % 2 == 0) {   // カーソル（0.5秒ごとに点滅）
            c.fillRect(TEXT_X + cursorX, y, 2, 13, lilac);
        }
    }

    // 候補の帯（下の案内の帯の上）
    int by = SCREEN_H - BOTTOM_H;
    if (candBand) {
        int cy = by - CAND_H;
        c.fillRect(0, cy, SCREEN_W, CAND_H, rgb(0xF3, 0xEE, 0xFC));
        if (ime.mode == 2) {
            drawCandidates(c, cy);
        } else {
            c.setFont(&fonts::efontJA_12);
            c.setTextColor(rgb(0x7E, 0x57, 0xC2));
            c.setTextDatum(middle_left);
            c.drawString(notice, TEXT_X, cy + CAND_H / 2);
        }
    }

    // 下の帯: キーの案内（いまの状態で変える）
    c.fillRect(0, by, SCREEN_W, BOTTOM_H, rgb(0xEE, 0xF3, 0xF7));
    const char* guide = ime.mode == 1   ? "Space 変換 Enter かなで決定"
                        : ime.mode == 2 ? "Space 次 Enter 決定 ⌫ 読みへ"
                                        : "Aa+字 ▽ ^S 書く Tab A/あ Esc home";
    drawGuide(c, guide, 4, by + BOTTOM_H / 2, grey);
    c.setTextDatum(top_left);   // シェルとメモ帳は左上ぞろえで描くので戻しておく
}

String kanaSave() { return "kana"; }

}  // namespace

UiApp kanaApp = {"kana", kanaKey, kanaDraw, nullptr, kanaSave, nullptr};
