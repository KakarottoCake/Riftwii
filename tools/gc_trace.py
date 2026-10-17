# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prints the records of a GameCube trace (sd:/riftwii/gc/trace.bin).

The blob writes a 512-byte record now and then while a GameCube game runs
(runtime/gc/gc_rt.h, "The trace"). The word names come from the
gc_trace_word enum in gc_rt.h, so this reader follows the header.

    py -3.13 tools/gc_trace.py trace.bin [--all]

Without --all, words that are zero in every record are left out.
"""
import argparse
import os
import re
import struct

HEADER = os.path.join(os.path.dirname(__file__), '..', 'runtime', 'gc', 'gc_rt.h')
MAGIC = 0x52575452  # "RWTR"
RECORD_BYTES = 512
WHERE = {1: 'read', 2: 'pad', 3: 'ai', 4: 'ar', 5: 'jump', 6: 'beat', 7: 'fatal', 8: 'command'}
# Counters read better in decimal; everything else (addresses, registers) in hex.
DECIMAL = {'seq', 'commands', 'reads', 'read_bytes', 'sd_requests', 'failures', 'pad_reads', 'stream_mixes',
           'stream_refills', 'ai_dmas', 'ar_dmas', 'alarms', 'beats', 'guard_hits', 'bt_events', 'bt_acl',
           'card_reads', 'card_flushes', 'card_dirty', 'vi_configures', 'projections', 'stream_starved',
           'stream_fetch_busy', 'stream_buffered', 'orthographics', 'ar_waits', 'ar_wait_longest',
           'ar_wait_timeouts'}


def word_names():
    """The enum's names in order, with the gaps its '= X + n' entries leave."""
    text = open(HEADER).read()
    body = re.search(r'enum gc_trace_word \{(.*?)\};', text, re.S).group(1)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    names = []
    for entry in (e.strip() for e in body.split(',')):
        if not entry:
            continue
        name, _, value = entry.partition('=')
        name = name.strip()
        if value:
            # An array of words: its first name stands for all of them,
            # numbered here (crash_chain, crash_chain_1, ...).
            base, _, extra = value.partition('+')
            first = names.index(base.strip())
            index = first + int(extra or 0, 0)
            while len(names) < index:
                names.append('%s_%d' % (base.strip(), len(names) - first))
        names.append(name)
    names.remove('GC_TR_WORDS')
    return [n.replace('GC_TR_', '').lower() for n in names]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('trace')
    parser.add_argument('--all', action='store_true', help='show words that are zero everywhere too')
    args = parser.parse_args()

    names = word_names()
    data = open(args.trace, 'rb').read()
    records = []
    for at in range(0, len(data) - RECORD_BYTES + 1, RECORD_BYTES):
        words = struct.unpack_from('>%dI' % len(names), data, at)
        if words[0] == MAGIC:
            records.append(words)
    records.sort(key=lambda words: words[1])
    print('%d record(s)' % len(records))

    shown = [i for i, name in enumerate(names)
             if name not in ('magic', 'tbu') and (args.all or any(r[i] for r in records))]
    for record in records:
        fields = []
        for i in shown:
            name, value = names[i], record[i]
            if name == 'where':
                fields.append('where=%s' % WHERE.get(value, value))
            elif name in DECIMAL:
                fields.append('%s=%d' % (name, value))
            else:
                fields.append('%s=0x%x' % (name, value))
        print(' '.join(fields))


if __name__ == '__main__':
    main()
