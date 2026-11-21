// 日本語入力の部品（v1.1 単位1: ローマ字 → ひらがな、単位2: 漢字（SKK 方式 ▽▼）、単位3: 送りがな・学習）。画面とは切り離してあるので、Kana・Ask・メモ帳のどれからでも使える
#pragma once
#include <Arduino.h>

// 辞書を引く。yomi の候補を out に max 個まで入れて数を返す（なければ 0）
int skkLookup(const String& yomi, String* out, int max);

// 起動のとき /.ime-learn を読む（setup() の histLoad() のあと）
void imeLoadLearn();

struct Ime {
    static constexpr int MAX_CANDS = 16;

    String text;        // 確定した文字（UTF-8）
    String roma;        // まだかなにしていない英字
    bool kana = true;   // true = ひらがな、false = 英字

    // 変換（SKK をやさしくした形）
    int mode = 0;       // 0 = ふつう、1 = ▽（読みを打っている）、2 = ▼（候補を選んでいる）
    String yomi;        // ▽ の読み（ひらがな）
    String okuriHead;   // 送りがなの頭のローマ字1文字（KaU なら u）。空 = 送りがなではない
    String okuri;       // 送りがなのかな（う）
    String lookKey;     // いま引いた見出し（か・かu）。学習に使う
    String cands[MAX_CANDS];
    int candCount = 0;
    int candIdx = 0;
    bool notFound = false;   // Space で引いたが辞書に無かった（画面が見たら false に戻す）

    void imeKey(char c);     // 1文字打つ（かなモードの大文字は ▽ の始まり）
    void imeSpace();         // mode 0: 空白、mode 1: 変換、mode 2: 次の候補
    bool imeEnter();         // 変換を確定したら true、何もしなかったら false
    bool imeEscape();        // 変換をやめたら（読みをひらがなのまま確定）true
    void imeBackspace();     // mode 2: ▽へ戻る、mode 1: roma → yomi の順に消す、mode 0: roma → text の順に消す
    void imeToggle();        // ひらがな ⇔ 英字（変換中なら確定し、roma の残りはそのまま text へ）
    void imeClear();         // 全部消す

private:
    void romaKey(char c);    // ローマ字をかなにして、mode 1 なら yomi、それ以外は text に足す
    void flushRoma();        // roma の残りを出しきる（n だけなら「ん」）
    void commit();           // mode 1・2 を確定して mode 0 へ
    String& out();           // ローマ字をかなにした行き先（text・yomi・okuri）
    void lookup(const String& key);   // key で辞書を引く。あれば mode 2
    void lookupOkuri();      // 送りがなが決まったら yomi + okuriHead で引く。なければ yomi に足して ▽ のまま
    void clearOkuri();
};
