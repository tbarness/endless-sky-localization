#!/usr/bin/env python3
"""Prepare a translated .po for Endless Sky: add a menu name, and compile the .mo.

Usage:
  python utils/prepare_language.py INPUT.po OUTPUT_FOLDER [--name "Display Name"] [--ascii]

Creates  OUTPUT_FOLDER/LC_MESSAGES/endless-sky.po  and  endless-sky.mo.
Name the output folder after the language, e.g. languages/ja. The folder name is the
language's code, and is also the name of the folder in "fonts" that holds that language's fonts.

The text is kept exactly as written. The game draws accents, Cyrillic, Greek, Chinese,
Japanese, Korean, ... from font files, so put a font that has the characters in the "fonts"
folder (see fonts/README.txt). Without one, those characters are drawn as blanks.

--ascii is only for a game with no font file at all: the built-in font image has no accents,
so this replaces them by plain letters (a, n, ...) and drops what has no ASCII form, which
makes Chinese, Japanese and Korean text unreadable.
"""
import argparse
import ast
import os
import re
import struct
import sys
import unicodedata

PUNCT = {"¿": "", "¡": "", "«": '"', "»": '"', "“": '"', "”": '"', "‘": "'", "’": "'",
         "—": "-", "–": "-", "…": "...", "\u2068": "", "\u2069": "", "\u00a0": " ",
         "º": "o", "ª": "a", "€": "EUR", "ß": "ss", "æ": "ae", "Æ": "AE", "œ": "oe", "Œ": "OE",
         "ø": "o", "Ø": "O", "ł": "l", "Ł": "L", "đ": "d", "Đ": "D"}


def fold(text):
    text = "".join(PUNCT.get(c, c) for c in text)
    text = unicodedata.normalize("NFKD", text)
    text = "".join(c for c in text if not unicodedata.combining(c))
    return "".join(c if ord(c) < 128 else "?" for c in text)


def encode(text):
    return (text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
            .replace("\t", "\\t").replace("\r", "\\r"))


def literal(line):
    """Decode the quoted string in a PO line such as:  msgstr "Hola"  or  "continued"."""
    return ast.literal_eval(line[line.index('"'):].strip())


def fold_po(lines, name, do_fold=True):
    """Fold every msgstr line (if do_fold), and add the X-Language-Name header."""
    out, in_str, has_name, changed = [], False, False, 0
    for line in lines:
        stripped = line.strip()
        if re.match(r"msgstr(\[\d+\])?\s", stripped):
            in_str = True
        elif stripped.startswith(("msgid", "msgctxt", "#")) or not stripped:
            in_str = False
        if do_fold and in_str and '"' in stripped and (stripped.startswith('"') or stripped.startswith("msgstr")):
            original = literal(stripped)
            folded = fold(original)
            if folded != original:
                changed += 1
            prefix = stripped[:stripped.index('"')]
            line = f'{prefix}"{encode(folded)}"\n'
        if stripped.startswith('"X-Language-Name:'):
            has_name = True
        out.append(line)
    if name and not has_name:
        for i, line in enumerate(out):
            if line.strip().startswith('"Language:'):
                out.insert(i + 1, f'"X-Language-Name: {encode(name)}\\n"\n')
                break
        else:
            sys.exit('The .po header has no "Language:" line to attach the name to.')
    return out, changed


def parse_po(lines):
    """Return {key: value} using the same key layout as .mo files (context, plurals)."""
    entries, cur, field = {}, {}, None

    def flush():
        nonlocal cur
        if "msgid" in cur and not cur.get("fuzzy"):
            key = cur["msgid"]
            if "ctx" in cur:
                key = cur["ctx"] + "\x04" + key
            if "plural" in cur:
                key += "\0" + cur["plural"]
                forms = [cur["str"][i] for i in sorted(cur["str"])]
                value = "\0".join(forms)
                translated = any(forms)
            else:
                value = cur["str"].get(0, "")
                translated = bool(value)
            if translated or cur["msgid"] == "" and "ctx" not in cur:
                entries[key] = value
        cur = {}

    for raw in lines:
        line = raw.strip()
        if not line:
            flush(); field = None; continue
        if line.startswith("#,") and "fuzzy" in line:
            cur["fuzzy"] = True; continue
        if line.startswith("#"):
            continue
        m = re.match(r"(msgctxt|msgid_plural|msgid|msgstr)(?:\[(\d+)\])?\s+(\".*\")$", line)
        if m:
            kind, index, text = m.group(1), int(m.group(2) or 0), ast.literal_eval(m.group(3))
            if kind == "msgid" and "msgid" in cur and "str" in cur:
                flush()
            if kind == "msgctxt": cur["ctx"] = text; field = ("ctx",)
            elif kind == "msgid": cur["msgid"] = text; field = ("msgid",)
            elif kind == "msgid_plural": cur["plural"] = text; field = ("plural",)
            else: cur.setdefault("str", {})[index] = text; field = ("str", index)
        elif line.startswith('"') and field:
            text = ast.literal_eval(line)
            if len(field) == 1: cur[field[0]] += text
            else: cur["str"][field[1]] += text
    flush()
    return entries


def write_mo(path, entries):
    """Write a GNU .mo file (little endian, sorted, as msgfmt does)."""
    keys = sorted(k.encode("utf-8") for k in entries)
    ids, strs, kidx, vidx = b"", b"", [], []
    values = {k.encode("utf-8"): v.encode("utf-8") for k, v in entries.items()}
    for k in keys:
        kidx.append((len(k), len(ids))); ids += k + b"\0"
    for k in keys:
        v = values[k]; vidx.append((len(v), len(strs))); strs += v + b"\0"
    n = len(keys); ks = 28 + n * 16; vs = ks + len(ids)
    data = struct.pack("<IIIIIII", 0x950412DE, 0, n, 28, 28 + n * 8, 0, 0)
    data += b"".join(struct.pack("<II", l, o + ks) for l, o in kidx)
    data += b"".join(struct.pack("<II", l, o + vs) for l, o in vidx)
    with open(path, "wb") as f:
        f.write(data + ids + strs)
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input"); ap.add_argument("output")
    ap.add_argument("--name", default="", help="name shown on the menu, in the language's own script if you like")
    ap.add_argument("--ascii", action="store_true", help="replace non-ASCII characters (only for a game without font files)")
    ap.add_argument("--keep-accents", action="store_true", help=argparse.SUPPRESS)   # the old option; keeping text is now the default
    args = ap.parse_args()

    with open(args.input, encoding="utf-8") as f:
        lines = f.readlines()
    lines, changed = fold_po(lines, args.name, do_fold=args.ascii)
    folder = os.path.join(args.output, "LC_MESSAGES")
    os.makedirs(folder, exist_ok=True)
    po_path = os.path.join(folder, "endless-sky.po")
    with open(po_path, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(lines)
    count = write_mo(os.path.join(folder, "endless-sky.mo"), parse_po(lines))
    print(f"{count} entries compiled to {os.path.join(folder, 'endless-sky.mo')}"
          + (f"  ({changed} lines folded to ASCII)" if args.ascii else ""))
    if args.ascii:
        lost = sum(line.count("?") for line in lines) - sum(line.count("?") for line in open(args.input, encoding="utf-8"))
        if lost > 0:
            print(f"WARNING: {lost} character(s) have no ASCII form and became '?'. Use a font file instead of --ascii.")
    else:
        # Scripts that need a font file, which the player has to provide.
        needs = sorted({c for line in lines if line.lstrip().startswith(("msgstr", '"')) for c in line if ord(c) >= 0x250})
        if needs:
            print(f"Note: this language uses {len(needs)} characters that need a font file (for example {''.join(needs[:8])}). "
                  f"Put one in the fonts folder: fonts/{os.path.basename(os.path.normpath(args.output))}/ for this language only. "
                  "See fonts/README.txt.")


if __name__ == "__main__":
    main()
