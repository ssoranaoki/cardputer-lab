# SKK-JISYO.M から src/edit1/skkdict.h（Cardputer に埋め込む辞書）を作る（v1.1 単位2）
# 使い方: python tools\make_skkdict.py
#   1. third_party/skk/SKK-JISYO.M が無ければ GitHub から取ってきて、そのまま（EUC-JP のまま）保存する
#   2. EUC-JP → UTF-8 に読み、; で始まる行（説明・GPL の文）は飛ばす。候補の ;（注釈）から後ろは消す
#   3. 読みの UTF-8 のバイト順に並べ（Cardputer 側の二分探索がこの順を前提にする）、skkdict.h に書き出す
import os
import urllib.request

URL = "https://raw.githubusercontent.com/skk-dev/dict/master/SKK-JISYO.M"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "third_party", "skk", "SKK-JISYO.M")
OUT = os.path.join(ROOT, "src", "edit1", "skkdict.h")


def fetch():
    if os.path.exists(SRC):
        return
    os.makedirs(os.path.dirname(SRC), exist_ok=True)
    print("download", URL)
    with urllib.request.urlopen(URL) as r:
        data = r.read()
    with open(SRC, "wb") as f:   # バイトのまま保存する（説明の行も消さない）
        f.write(data)


def parse():
    entries = {}   # 読み → 候補のリスト（同じ読みが2つあればつなぐ）
    with open(SRC, "rb") as f:
        text = f.read().decode("euc_jp", errors="replace")
    for line in text.splitlines():
        if not line or line.startswith(";"):
            continue
        yomi, sep, rest = line.partition(" /")
        if not sep:
            continue
        cands = entries.setdefault(yomi, [])
        for c in rest.split("/"):
            c = c.split(";", 1)[0]
            if c and c not in cands:
                cands.append(c)
    return entries


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def write(entries):
    keys = sorted(entries, key=lambda k: k.encode("utf-8"))
    lines = []
    for k in keys:
        if entries[k]:
            lines.append(k + " /" + "/".join(entries[k]) + "/")
    offs = []
    pos = 0
    for ln in lines:
        offs.append(pos)
        pos += len(ln.encode("utf-8")) + 1   # + 1 は \n
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// SKK-JISYO.M（GPL v2 以降）から tools\\make_skkdict.py で作った。手で直さない\n")
        f.write("// 出典とライセンス: third_party/skk/README.md\n")
        f.write("// 1行1語「読み /候補1/候補2/…/」。読みの UTF-8 のバイト順に並んでいる（ime.cpp の skkLookup が二分探索する）\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("const char SKK_DICT[] =\n")
        for ln in lines:
            f.write('    "' + c_escape(ln) + '\\n"\n')
        f.write(";\n\n")
        f.write("const uint32_t SKK_OFF[] = {\n")
        for i in range(0, len(offs), 10):
            f.write("    " + ", ".join(str(o) for o in offs[i:i + 10]) + ",\n")
        f.write("};\n\n")
        f.write("const int SKK_COUNT = %d;\n" % len(lines))
    print("words:", len(lines), " dict bytes:", pos, " ->", OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    fetch()
    write(parse())
