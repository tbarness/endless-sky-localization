#!/usr/bin/env python3
"""Build a gettext template (.pot) from Endless Sky interface files.

Usage:  python utils/extract_pot.py OUTPUT.pot INTERFACE.txt [MORE.txt ...]

Only static text is extracted: "label", "button" and their "wrapped" variants.
"string" and "dynamic button" elements hold runtime values, not English text.
"""
import datetime
import sys

STATIC = {"label": 1, "wrapped label": 1, "button": 2, "wrapped button": 2}


def tokenize(line):
    """Split a data line the way the game does: words, or "quoted" / `quoted` text."""
    tokens, i = [], 0
    while i < len(line):
        c = line[i]
        if c in " \t":
            i += 1
        elif c == "#":
            break
        elif c in '"`':
            end = line.find(c, i + 1)
            if end < 0:
                end = len(line)
            tokens.append(line[i + 1:end])
            i = end + 1
        else:
            end = i
            while end < len(line) and line[end] not in " \t":
                end += 1
            tokens.append(line[i:end])
            i = end
    return tokens


def quote(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    output, sources = sys.argv[1], sys.argv[2:]

    entries = {}  # msgid -> (references, hotkeys), in order of first appearance
    for path in sources:
        with open(path, encoding="utf-8") as f:
            for number, raw in enumerate(f, 1):
                tokens = tokenize(raw.rstrip("\n"))
                if not tokens:
                    continue
                # "wrapped label" is two words, so try the longest key first.
                for key in sorted(STATIC, key=len, reverse=True):
                    words = key.split()
                    if tokens[:len(words)] == words:
                        # A button's first token after the keyword is its hotkey.
                        index = len(words) + (1 if words[-1] == "button" else 0)
                        if len(tokens) > index and tokens[index]:
                            refs, hotkeys = entries.setdefault(tokens[index], ([], set()))
                            refs.append(f"{path.replace(chr(92), '/')}:{number}")
                            if words[-1] == "button" and len(tokens) > len(words):
                                hotkeys.add(tokens[len(words)])
                        break

    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M%z")
    with open(output, "w", encoding="utf-8", newline="\n") as out:
        out.write("# Endless Sky translation template.\n")
        out.write('msgid ""\nmsgstr ""\n')
        out.write('"Project-Id-Version: Endless Sky\\n"\n')
        out.write(f'"POT-Creation-Date: {now}\\n"\n')
        out.write('"MIME-Version: 1.0\\n"\n')
        out.write('"Content-Type: text/plain; charset=UTF-8\\n"\n')
        out.write('"Content-Transfer-Encoding: 8bit\\n"\n')
        out.write('"X-Language-Name: \\n"\n\n')
        for msgid, (refs, hotkeys) in entries.items():
            if hotkeys and "_" in msgid:
                out.write("#. Button hotkey: %s. Keep a \"_\" in front of that letter if it appears in the\n"
                          % "/".join(sorted(hotkeys)))
                out.write("#. translation. Otherwise add it at the end, like \"Quit (_Q)\".\n")
            out.write("#: " + " ".join(refs[:4]) + "\n")
            out.write(f"msgid {quote(msgid)}\nmsgstr \"\"\n\n")
    print(f"Wrote {len(entries)} messages to {output}")


if __name__ == "__main__":
    main()
