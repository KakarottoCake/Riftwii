# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Split the menu's translations for the start-up stash (Makefile.wii).

    python3 tools/split_po.py ids wii/lang/*.po      > msgids.txt
    python3 tools/split_po.py strings wii/lang/es.po > es.str
    python3 tools/split_po.py test

Every wii/lang/*.po lists the same English entries in the same order
(tools/lang_source.py writes them all), so the stash keeps the English
"msgid" lines once and each language's "msgstr" lines alone, 126 KiB less
than six whole files. wii/core/i18n.cpp pairs them up again line by line.
"ids" checks that every file still lists the same entries, and both modes
refuse a multi-line entry, which line-by-line pairing would get wrong.
"""
import sys


def entry_lines(text, name):
    """The msgid and msgstr lines of a .po file, in order."""
    ids, strings = [], []
    for number, line in enumerate(text.split("\n"), 1):
        if line.startswith("msgid "):
            ids.append(line)
        elif line.startswith("msgstr "):
            strings.append(line)
        elif line.startswith('"') or line.startswith("msgid_plural") or line.startswith("msgctxt"):
            raise ValueError("%s:%d: only one-line msgid/msgstr entries can be split" % (name, number))
    if len(ids) != len(strings):
        raise ValueError("%s: %d msgid lines but %d msgstr lines" % (name, len(ids), len(strings)))
    return ids, strings


def common_ids(files):
    """The msgid lines all files share, or an error naming the first that differs."""
    first = None
    for name, text in files:
        ids, _ = entry_lines(text, name)
        if first is None:
            first = (name, ids)
        elif ids != first[1]:
            raise ValueError("%s does not list the same entries as %s: run tools/lang_source.py" % (name, first[0]))
    return first[1]


def self_test():
    a = 'msgid ""\nmsgstr "x"\n\nmsgid "Hi"\nmsgstr "Hola"\n'
    b = 'msgid ""\nmsgstr "y"\n\nmsgid "Hi"\nmsgstr "Salut"\n'
    assert common_ids([("a", a), ("b", b)]) == ['msgid ""', 'msgid "Hi"']
    assert entry_lines(b, "b")[1] == ['msgstr "y"', 'msgstr "Salut"']
    for bad in ([("a", a), ("c", 'msgid "Other"\nmsgstr "z"\n')],
                [("d", 'msgid "Hi"\n"there"\nmsgstr "x"\n')]):
        try:
            common_ids(bad)
        except ValueError:
            continue
        raise AssertionError("accepted %r" % bad)
    print("split_po: self-test passed")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "test":
        self_test()
        return
    if len(sys.argv) < 3 or sys.argv[1] not in ("ids", "strings"):
        sys.exit(__doc__)
    files = [(path, open(path, encoding="utf-8").read()) for path in sys.argv[2:]]
    try:
        if sys.argv[1] == "ids":
            lines = common_ids(files)
        else:
            if len(files) != 1:
                sys.exit("split_po: strings takes one file")
            lines = entry_lines(files[0][1], files[0][0])[1]
    except ValueError as error:
        sys.exit("split_po: %s" % error)
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stdout.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
