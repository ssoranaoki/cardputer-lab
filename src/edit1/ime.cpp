// ローマ字 → ひらがな（v1.1 単位1）、漢字（v1.1 単位2、SKK 方式）
// 打った英字を roma にためておき、表と合ったらかなに置きかえる。ky のように途中ならまだ待つ
// 大文字で ▽（読みを打つ）が始まり、Space で辞書を引いて ▼（候補を選ぶ）になる
// ▽ の中の大文字は送りがなの始まり（KaU → 買う）。送りがなのかなが決まったら「読み＋頭のローマ字」で引く（v1.1 単位3）
#include <LittleFS.h>
#include "ime.h"
#include "shell.h"
#include "skkdict.h"

namespace {

struct Roma {
    const char* key;
    const char* kana;
};

const Roma TABLE[] = {
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"ta", "た"}, {"ti", "ち"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
    {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
    {"ha", "は"}, {"hi", "ひ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
    {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
    {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
    {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
    {"wa", "わ"}, {"wo", "を"},
    {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
    {"za", "ざ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
    {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
    {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
    {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
    {"shi", "し"}, {"chi", "ち"}, {"tsu", "つ"}, {"fu", "ふ"}, {"ji", "じ"},
    {"kya", "きゃ"}, {"kyu", "きゅ"}, {"kyo", "きょ"},
    {"sya", "しゃ"}, {"syu", "しゅ"}, {"syo", "しょ"},
    {"sha", "しゃ"}, {"shu", "しゅ"}, {"sho", "しょ"},
    {"tya", "ちゃ"}, {"tyu", "ちゅ"}, {"tyo", "ちょ"},
    {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"},
    {"nya", "にゃ"}, {"nyu", "にゅ"}, {"nyo", "にょ"},
    {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"},
    {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"},
    {"rya", "りゃ"}, {"ryu", "りゅ"}, {"ryo", "りょ"},
    {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"},
    {"zya", "じゃ"}, {"zyu", "じゅ"}, {"zyo", "じょ"},
    {"ja", "じゃ"}, {"ju", "じゅ"}, {"jo", "じょ"},
    {"dya", "ぢゃ"}, {"dyu", "ぢゅ"}, {"dyo", "ぢょ"},
    {"bya", "びゃ"}, {"byu", "びゅ"}, {"byo", "びょ"},
    {"pya", "ぴゃ"}, {"pyu", "ぴゅ"}, {"pyo", "ぴょ"},
    {"xa", "ぁ"}, {"xi", "ぃ"}, {"xu", "ぅ"}, {"xe", "ぇ"}, {"xo", "ぉ"},
    {"xya", "ゃ"}, {"xyu", "ゅ"}, {"xyo", "ょ"}, {"xtu", "っ"}, {"ltu", "っ"},
    {"n'", "ん"}, {"-", "ー"}, {",", "、"}, {".", "。"},
};

// UTF-8 の最後の1文字を消す（10xxxxxx は続きのバイト。先頭のバイトまで戻る）
void removeLastChar(String& s) {
    int i = s.length() - 1;
    if (i < 0) return;
    while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
    s.remove(i);
}

// 辞書の i 行目の読みと yomi を比べる（strcmp と同じく、負・0・正）。読みは行の頭から空白まで
int compareYomi(int i, const char* y, int ylen) {
    const char* line = SKK_DICT + SKK_OFF[i];
    int len = strchr(line, ' ') - line;
    int r = memcmp(line, y, min(len, ylen));
    if (r != 0) return r;
    return len - ylen;
}

bool isVowel(char c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }
bool isConsonant(char c) { return c >= 'a' && c <= 'z' && !isVowel(c); }

const char* findExact(const String& r) {
    for (const auto& e : TABLE) {
        if (r == e.key) return e.kana;
    }
    return nullptr;
}

// 表の見出しのどれかが r で始まるか（まだ続きを待つ価値があるか）
bool anyStartsWith(const String& r) {
    for (const auto& e : TABLE) {
        if (strncmp(e.key, r.c_str(), r.length()) == 0) return true;
    }
    return false;
}

// ---- 学習: 選んだ候補を書き足しで覚え、次に同じ読みを引いたら一番前に出す（学習） ----
constexpr const char* LEARN_FILE = "/.ime-learn";
constexpr int LEARN_MAX = 200;         // RAM の表に覚える見出しの数
constexpr int LEARN_FILE_MAX = 400;    // 起動のとき行がこれをこえていたら、表の分だけに書き直す
String learnKey[LEARN_MAX];
String learnVal[LEARN_MAX];
int learnCount = 0;

int learnFind(const String& key) {
    for (int i = 0; i < learnCount; i++) {
        if (learnKey[i] == key) return i;
    }
    return -1;
}

// 表に入れる。同じ見出しは上書き、いっぱいならいちばん古いのを捨てる
void learnSet(const String& key, const String& val) {
    int i = learnFind(key);
    if (i >= 0) {
        learnVal[i] = val;
        return;
    }
    if (learnCount == LEARN_MAX) {
        for (int k = 1; k < LEARN_MAX; k++) {
            learnKey[k - 1] = learnKey[k];
            learnVal[k - 1] = learnVal[k];
        }
        learnCount--;
    }
    learnKey[learnCount] = key;
    learnVal[learnCount] = val;
    learnCount++;
}

// 確定した候補を表とファイルに覚える（ファイルは後ろに1行足すだけ）
void learnPush(const String& key, const String& val) {
    if (key.length() == 0) return;
    learnSet(key, val);
    if (!fsIsReady()) return;
    File f = LittleFS.open(LEARN_FILE, FILE_APPEND);
    if (!f) return;
    f.print(key + "\t" + val + "\n");
    f.close();
}

// 表にその見出しがあれば、その候補を一番前に動かす
void learnApply(const String& key, String* cands, int n) {
    int i = learnFind(key);
    if (i < 0) return;
    for (int k = 1; k < n; k++) {
        if (cands[k] == learnVal[i]) {
            String c = cands[k];
            for (int j = k; j > 0; j--) cands[j] = cands[j - 1];
            cands[0] = c;
            return;
        }
    }
}

}  // namespace

// 起動のとき: /.ime-learn を1行ずつ読む（見出し<TAB>候補）。同じ見出しはあとの行で上書きされ、最後に選んだ候補が残る
void imeLoadLearn() {
    if (!fsIsReady()) return;
    int lines = 0;
    File f = LittleFS.open(LEARN_FILE);
    if (f && !f.isDirectory()) {
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            lines++;
            int tab = line.indexOf('\t');
            if (tab > 0 && tab < (int)line.length() - 1) learnSet(line.substring(0, tab), line.substring(tab + 1));
        }
    }
    if (f) f.close();
    if (lines > LEARN_FILE_MAX) {      // 長くなったら、表の中身だけに詰め直す
        f = LittleFS.open(LEARN_FILE, FILE_WRITE);
        if (f) {
            for (int i = 0; i < learnCount; i++) f.print(learnKey[i] + "\t" + learnVal[i] + "\n");
            f.close();
        }
    }
    Serial.printf("ime: learned %d from /.ime-learn (%d lines)\n", learnCount, lines);
}

// 読みの順に並んだ辞書を、真ん中と比べて半分ずつ絞り込む（二分探索）。8,300語でも約13回で見つかる
int skkLookup(const String& yomi, String* out, int max) {
    if (yomi.length() == 0) return 0;
    int lo = 0, hi = SKK_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int r = compareYomi(mid, yomi.c_str(), yomi.length());
        if (r < 0) {
            lo = mid + 1;
        } else if (r > 0) {
            hi = mid - 1;
        } else {
            // 見つかった: 「読み /候補1/候補2/…/」の / と / のあいだを取り出す
            const char* p = strchr(SKK_DICT + SKK_OFF[mid], '/') + 1;
            int n = 0;
            while (n < max && *p != '\n' && *p != '\0') {
                const char* q = strchr(p, '/');
                if (!q) break;
                String c;
                c.concat(p, q - p);
                out[n++] = c;
                p = q + 1;
            }
            return n;
        }
    }
    return 0;
}

void Ime::imeKey(char c) {
    if (!kana) {
        text += c;
        return;
    }
    if (c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
        if (mode == 0) {          // ▽ を始める（打ちかけのローマ字は先に出しきる）
            flushRoma();
            mode = 1;
            yomi = "";
        } else if (mode == 2) {   // いまの候補を確定してから、新しい ▽
            commit();
            mode = 1;
            yomi = "";
        } else if (okuriHead.length() == 0 && yomi.length() > 0) {
            // ▽ の中の大文字: ここから送りがな（打ちかけが n だけで、続きが母音・y・n でなければ「ん」にしてから）
            if (roma == "n" && !isVowel(c) && c != 'y' && c != 'n') flushRoma();
            if (roma.length() == 0) {
                okuriHead = String(c);
                okuri = "";
            }
        }
        // 打ちかけのローマ字の途中・送りがなの中の大文字は、ふつうの小文字
    } else if (mode == 2) {
        commit();                 // 候補を確定してから、その文字をふつうに打つ
    }
    romaKey(c);
    if (mode == 1 && okuriHead.length() > 0 && roma.length() == 0 && okuri.length() > 0) lookupOkuri();
}

void Ime::lookup(const String& key) {
    lookKey = key;
    candCount = skkLookup(key, cands, MAX_CANDS);
    learnApply(key, cands, candCount);
    candIdx = 0;
    if (candCount > 0) mode = 2;
}

void Ime::lookupOkuri() {
    lookup(yomi + okuriHead);
    if (candCount > 0) return;
    yomi += okuri;   // 辞書に無ければ、送りがなも読みに入れて ▽ のまま
    clearOkuri();
    notFound = true;
}

void Ime::clearOkuri() {
    okuriHead = "";
    okuri = "";
}

void Ime::imeSpace() {
    if (mode == 0) {
        imeKey(' ');
    } else if (mode == 1) {
        flushRoma();
        if (okuriHead.length() > 0) {   // 送りがなの途中（KaK のあと Space など）
            if (okuri.length() > 0) {
                lookupOkuri();
            } else {
                clearOkuri();
                notFound = true;
            }
            return;
        }
        lookup(yomi);
        if (candCount == 0) notFound = true;
    } else {
        candIdx = (candIdx + 1) % candCount;   // 最後なら最初へ
    }
}

bool Ime::imeEnter() {
    if (mode == 0) return false;
    commit();
    return true;
}

bool Ime::imeEscape() {
    if (mode == 0) return false;
    if (mode == 1 && okuriHead.length() > 0) {   // 送りがなの途中: 送りがなを消して ▽ に戻る
        roma = "";
        clearOkuri();
        return true;
    }
    mode = 1;    // 候補を選んでいても、読みをひらがなのまま確定する
    commit();
    return true;
}

String& Ime::out() {
    if (mode == 1) return okuriHead.length() > 0 ? okuri : yomi;   // ▽ のあいだは読み（送りがなの中は okuri）にためる
    return text;
}

void Ime::flushRoma() {
    if (roma.length() == 0) return;
    out() += roma == "n" ? String("ん") : roma;
    roma = "";
}

void Ime::commit() {
    if (mode == 2) {
        text += cands[candIdx] + okuri;
        learnPush(lookKey, cands[candIdx]);
    } else if (mode == 1) {
        flushRoma();
        text += yomi + okuri;
    }
    yomi = "";
    clearOkuri();
    lookKey = "";
    candCount = 0;
    candIdx = 0;
    mode = 0;
}

void Ime::romaKey(char c) {
    String& out = this->out();
    roma += c;
    while (roma.length() > 0) {
        // b) 同じ子音が2つ（kk・tt など）: 「っ」にして1つ目を消す
        if (roma.length() >= 2 && roma[0] == roma[1] && isConsonant(roma[0]) && roma[0] != 'n') {
            out += "っ";
            roma.remove(0, 1);
            continue;
        }
        // c) n の次が母音・y・n でない: 「ん」にして n を消す（' は n' で「ん」にするので除く）
        if (roma[0] == 'n' && roma.length() >= 2) {
            char c2 = roma[1];
            if (c2 == 'n') {
                out += "ん";
                roma = "";
                break;
            }
            if (!isVowel(c2) && c2 != 'y' && c2 != '\'') {
                out += "ん";
                roma.remove(0, 1);
                continue;
            }
        }
        // d) 表にあればかなに
        if (const char* k = findExact(roma)) {
            out += k;
            roma = "";
            break;
        }
        // e) どの見出しにもつながらない: 1文字目はそのまま出し、残りでやり直す
        if (!anyStartsWith(roma)) {
            out += roma[0];
            roma.remove(0, 1);
            continue;
        }
        break;   // 途中（ky など）: 続きを待つ
    }
}

void Ime::imeBackspace() {
    if (mode == 2) {   // 候補をやめて ▽ の読みに戻る（送りがなも消す）
        mode = 1;
        candCount = 0;
        clearOkuri();
        return;
    }
    if (mode == 1 && okuriHead.length() > 0) {   // 送りがなの途中: 送りがなを消して ▽ に戻る
        roma = "";
        clearOkuri();
        return;
    }
    if (roma.length() > 0) {
        roma.remove(roma.length() - 1);
    } else if (mode == 1) {
        removeLastChar(yomi);
    } else {
        removeLastChar(text);
        return;
    }
    if (mode == 1 && yomi.length() == 0 && roma.length() == 0) mode = 0;
}

void Ime::imeToggle() {
    commit();
    text += roma;
    roma = "";
    kana = !kana;
}

void Ime::imeClear() {
    text = "";
    roma = "";
    yomi = "";
    clearOkuri();
    candCount = 0;
    candIdx = 0;
    mode = 0;
}
