// ホーム画面（v0.9 単位1、案C フラットな大きいタイル。v1.1 で Kana を足して4つ）。fn＋, fn＋/ で選び、Enter で開く
#include <M5Cardputer.h>
#include "shell.h"
#include "uiapp.h"

namespace {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int BAR_H = 12;      // 上の帯
constexpr int TILE = 44;       // タイルの大きさ
constexpr int TILE_R = 10;     // 角丸の半径
constexpr int GAP = 12;        // タイルの間
constexpr int TILE_Y = BAR_H + 22;
constexpr int TILE_COUNT = 4;
constexpr int TILE_X0 = (SCREEN_W - (TILE * TILE_COUNT + GAP * (TILE_COUNT - 1))) / 2;  // 真ん中にそろえる

const char* const names[TILE_COUNT] = {"Shell", "Files", "Memo", "Kana"};
int selected = 0;                 // 選んでいるタイル

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return M5Cardputer.Display.color565(r, g, b); }

// タイルの中の記号（白）。x, y はタイルの左上
void drawSymbol(M5Canvas& c, int i, int x, int y) {
    if (i == 0) {                        // Shell: ">_" を文字で
        c.setFont(&fonts::Font4);
        c.setTextColor(WHITE);
        c.setTextDatum(middle_center);
        c.drawString(">_", x + TILE / 2, y + TILE / 2);
    } else if (i == 1) {                 // Files: フォルダ（つまみと本体の角丸2つ）
        c.fillRoundRect(x + 9, y + 12, 12, 8, 2, WHITE);
        c.fillRoundRect(x + 9, y + 16, 26, 17, 3, WHITE);
    } else if (i == 2) {                 // Memo: 横線3本
        for (int k = 0; k < 3; k++) c.fillRoundRect(x + 11, y + 13 + k * 8, 22, 3, 1, WHITE);
    } else {                             // Kana: 「あ」
        c.setFont(&fonts::efontJA_16);
        c.setTextColor(WHITE);
        c.setTextDatum(middle_center);
        c.drawString("あ", x + TILE / 2, y + TILE / 2);
    }
}

void homeKey(const Keyboard_Class::KeysState& keys) {
    if (keys.left) {
        if (selected > 0) selected--;    // 端で止まる
    } else if (keys.right) {
        if (selected < TILE_COUNT - 1) selected++;
    } else if (keys.enter) {
        if (selected == 0) {
            openApp(&shellApp);
        } else if (selected == 1) {
            filesShow();
        } else if (selected == 2) {      // Memo: /memo.txt を開く（なければ新しいファイルとして）
            if (editOpen("/memo.txt")) openApp(&memoApp);
        } else {                         // Kana: 日本語入力の試し打ち
            openApp(&kanaApp);
        }
    }
    // fn＋; fn＋.（上下）は何もしない
}

void homeDraw(M5Canvas& c) {
    const uint16_t grey = rgb(0x80, 0x80, 0x80);
    const uint16_t tileColors[TILE_COUNT] = {rgb(0x6F, 0xD3, 0xB0), rgb(0x7C, 0xC2, 0xEE), rgb(0xF5, 0xA7, 0x7F), rgb(0xB3, 0x9D, 0xDB)};
    c.fillSprite(WHITE);

    // 上の帯: 左に名前、右に電池の残り
    c.fillRect(0, 0, SCREEN_W, BAR_H, rgb(0xEE, 0xF3, 0xF7));
    c.setFont(&fonts::Font0);
    c.setTextColor(grey);
    c.setTextDatum(middle_left);
    c.drawString("cardputer v1.1", 4, BAR_H / 2);
    char bat[8];
    int level = M5Cardputer.Power.getBatteryLevel();
    if (level < 0) snprintf(bat, sizeof(bat), "--");
    else snprintf(bat, sizeof(bat), "%d%%", level);
    c.setTextDatum(middle_right);
    c.drawString(bat, SCREEN_W - 4, BAR_H / 2);

    // タイルと名前
    for (int i = 0; i < TILE_COUNT; i++) {
        int x = TILE_X0 + i * (TILE + GAP);
        if (i == selected) {             // 選んでいるタイルは外側に水色の枠（太さ3）
            for (int t = 0; t < 3; t++) {
                int o = 3 + t;
                c.drawRoundRect(x - o, TILE_Y - o, TILE + o * 2, TILE + o * 2, TILE_R + o, rgb(0x4D, 0xD0, 0xE1));
            }
        }
        c.fillRoundRect(x, TILE_Y, TILE, TILE, TILE_R, tileColors[i]);
        drawSymbol(c, i, x, TILE_Y);
        c.setFont(&fonts::Font2);
        c.setTextColor(grey);
        c.setTextDatum(top_center);
        c.drawString(names[i], x + TILE / 2, TILE_Y + TILE + 8);
    }

    // キーの案内
    c.setFont(&fonts::Font0);
    c.setTextColor(grey);
    c.setTextDatum(bottom_center);
    c.drawString("fn+, fn+/ select Enter open", SCREEN_W / 2, SCREEN_H - 2);
    c.setTextDatum(top_left);            // シェルとメモ帳は左上ぞろえで描くので戻しておく
}

}  // namespace

String homeSave() { return "home"; }

UiApp homeApp = {"home", homeKey, homeDraw, nullptr, homeSave, nullptr};
