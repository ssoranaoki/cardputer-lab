// 画面のアプリ（v0.9）。1枚の画面とキーボードは、いま前面にあるアプリだけに渡す
// loop() は前面のアプリの key()・draw() を呼ぶだけ。ほかのアプリは前面に来るまで何もしない
#pragma once
#include <M5Cardputer.h>

// main.cpp には仕事（FreeRTOS のタスク）用の struct App があるので、こちらは UiApp
struct UiApp {
    const char* name;
    void (*key)(const Keyboard_Class::KeysState&);   // キーを1回ぶん受け取る
    void (*draw)(M5Canvas&);                         // 画面ぜんぶ（240×135）に描く
    void (*resume)();                                // 前面に戻ってきたときに呼ぶ（nullptr なら何もしない）
    String (*save)();                                // 自分の「いま」を1行の文字にする（nullptr なら name）
    bool (*restore)(const String& arg);              // save の行の名前より後ろから戻す。戻せたら true（nullptr なら名前だけで戻せる）
};

extern UiApp homeApp;    // home.cpp（ホーム画面）
extern UiApp shellApp;   // main.cpp（シェル）
extern UiApp filesApp;   // filer.cpp（ファイラー）
extern UiApp memoApp;    // editor.cpp（メモ帳）
extern UiApp kanaApp;    // kana.cpp（日本語入力の試し打ち）

// フォルダを読み直してからファイラーを前面に出す（filer.cpp）
void filesShow();

// 前面のアプリを替え、次の loop ですぐ描き直す印を立てる（main.cpp）
void switchTo(UiApp* app);
// いまの前面を積んでから app を開く（main.cpp）
void openApp(UiApp* app);
// 積んだ一番上に戻る。空ならホーム（main.cpp）
void goBack();
