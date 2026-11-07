#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
"""
extract_b43.py - Copies some definitions WITHOUT MODIFICATIONS from a Linux b43
source (main.c, dma.c, ...) into a separate C file.

Linux's main.c is tied to mac80211, request_firmware, IRQs: it cannot be
compiled as a whole on Mac OS 9. The chip initialization functions, however,
are independent: this script extracts them by name, so they stay identical
to the original and can be regenerated when b43 is updated.

Usage:
    python extract_b43.py <source.c> <list.txt> <output.c> [include...]

list.txt: one name per line (function, variable or #define); '#' = comment.
"""
import re
import sys

IDENT = re.compile(r"[A-Za-z_]\w*")


def split_toplevel(text):
    """Splits the file into top-level items: (start, end, text)."""
    items = []
    i, n = 0, len(text)
    start = 0
    depth = 0
    paren = 0
    while i < n:
        c = text[i]
        # start of a preprocessor line at depth 0
        if depth == 0 and c == '#' and (i == 0 or text[i - 1] == '\n') and \
                strip_comments(text[start:i]).strip() == "":
            j = i
            while True:
                k = text.find('\n', j)
                if k < 0:
                    k = n
                    break
                if text[k - 1] == '\\':
                    j = k + 1
                    continue
                break
            items.append((i, k, text[i:k]))
            i = k + 1
            start = i
            continue
        if text.startswith("/*", i):
            k = text.find("*/", i + 2)
            i = (k + 2) if k >= 0 else n
            continue
        if text.startswith("//", i):
            k = text.find("\n", i)
            i = k if k >= 0 else n
            continue
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == '\\':
                    i += 1
                i += 1
            i += 1
            continue
        if c == '(':
            paren += 1
        elif c == ')':
            paren -= 1
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                # end of a function or of an initializer/struct
                j = i + 1
                rest = text[j:j + 200]
                m = re.match(r"\s*(__\w+\s*(\([^)]*\))?\s*)?;", rest)
                if m:
                    j += m.end()
                    items.append((start, j, text[start:j]))
                    i = j
                    start = i
                    continue
                if not re.search(r"=\s*$", text[start:i].split('{')[0]):
                    items.append((start, j, text[start:j]))
                    i = j
                    start = i
                    continue
        elif c == ';' and depth == 0 and paren == 0:
            items.append((start, i + 1, text[start:i + 1]))
            start = i + 1
        i += 1
    return items


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    return re.sub(r"//[^\n]*", " ", s)


def item_name(body):
    code = strip_comments(body).strip()
    if code.startswith('#'):
        m = re.match(r"#\s*define\s+(\w+)", code)
        return m.group(1) if m else None
    head = code.split('{')[0]
    if '(' in head and '=' not in head.split('(')[0]:
        before = head.split('(')[0]
        ids = IDENT.findall(before)
        return ids[-1] if ids else None
    head = re.split(r"[=\[;]", head)[0]
    ids = IDENT.findall(head)
    return ids[-1] if ids else None


def leading_comment(text, start):
    """/* ... */ comment right above the item (blank lines excluded)."""
    before = text[:start].rstrip()
    if before.endswith("*/"):
        k = before.rfind("/*")
        if k >= 0 and before[:k].endswith("\n") or k == 0:
            return before[k:] + "\n"
    return ""


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, listfile, out = sys.argv[1], sys.argv[2], sys.argv[3]
    includes = sys.argv[4:]
    text = open(src, encoding="utf-8").read().replace("\r\n", "\n")
    wanted = []
    for line in open(listfile, encoding="utf-8"):
        line = line.split('#', 1)[0].strip()
        if line:
            wanted.append(line)

    items = split_toplevel(text)
    found = {}
    for (s, e, body) in items:
        name = item_name(body)
        if name in wanted:
            has_body = '{' in strip_comments(body)
            if name in found:
                # prefer the definition (with a body) over the forward
                # declaration; between two definitions keep the first
                if found[name][3] or not has_body:
                    continue
            found[name] = (s, e, body.strip("\n"), has_body)

    missing = [w for w in wanted if w not in found]
    if missing:
        sys.exit("Not found in %s: %s" % (src, ", ".join(missing)))

    out_lines = [
        "/*",
        " * GENERATED by Tools/extract_b43.py: do NOT edit by hand.",
        " * Definitions copied unmodified from Linux 5.15 %s." % "/".join(src.replace("\\", "/").split("/")[-2:]),
        " * List in %s." % listfile.replace("\\", "/").split("/")[-1],
        " */",
    ]
    for inc in includes:
        out_lines.append('#include "%s"' % inc)
    out_lines.append("")
    for (s, e, body, _) in sorted(found.values()):
        out_lines.append(leading_comment(text, s) + body)
        out_lines.append("")
    with open(out, "w", newline="") as f:
        f.write("\r\n".join(out_lines).replace("\n", "\r\n").replace("\r\r\n", "\r\n"))
    print("Extracted %d definitions into %s" % (len(found), out))


if __name__ == "__main__":
    main()
