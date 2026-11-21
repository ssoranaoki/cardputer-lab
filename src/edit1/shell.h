// edit1 の部品どうしで分け合うもの（main.cpp・lineedit.cpp・files.cpp・editor.cpp）
#pragma once
#include <Arduino.h>

// main.cpp
extern String input;                                   // いま打っている1行
void printLine(const String& text, uint16_t color);    // 下の段に1行足す
int appCount();                                        // 仕事（アプリ）の数
const char* appName(int i);                            // 仕事の名前

// lineedit.cpp
extern int histPos;                                    // いま見ている履歴（-1 = 打ちかけの新しい行）
void histLoad();                                       // 起動のとき /.history を読む
void histPush(const String& line);
void histUp();
void histDown();
void cmdHistory();
void complete();

// files.cpp（内蔵フラッシュの LittleFS）
void fsInit();
void cmdLs(const String& arg);
void cmdCat(const String& arg);
void cmdEcho(const String& arg);
void cmdMkdir(const String& arg);
void cmdCd(const String& arg);
void cmdPwd();
const String& currentDir();                            // いまいるフォルダ（プロンプトに出す）
void cmdRm(const String& arg);
int fileNames(const String& dir, String* out, int max);   // Tab の補完用（フォルダには / が付く）
String fullPath(const String& arg);                    // いまいるフォルダから数えた、/ で始まる名前に直す
bool folderExists(const String& path);
bool fsIsReady();

// editor.cpp（メモ帳）
#include <M5Cardputer.h>
bool editOpen(const String& arg);                      // edit <ファイル>。開けたら true
bool editorActive();                                   // メモ帳が開いているか
void editorKey(const Keyboard_Class::KeysState& keys); // キーを1回ぶん渡す
void editorDraw(M5Canvas& canvas);                     // 画面ぜんぶ（240×135）に描く
