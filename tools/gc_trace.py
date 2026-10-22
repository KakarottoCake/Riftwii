# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prints the records of a GameCube trace (sd:/riftwii/gc/trace.bin).

The blob writes a 512-byte record now and then while a GameCube game runs
(runtime/gc/gc_rt.h, "The trace"). The word names come from the
gc_trace_word enum in gc_rt.h, so this reader follows the header.

    py -3.13 tools/gc_trace.py trace.bin [--all]

Without --all, words that are zero in every record are left out. In a run
with the test remote (sd:/riftwii/gc/test/fakeremote, or fakepro for a
Wii U Pro Controller) the projection_callers words hold its results instead
(gc_bt.h's fake_log). In a record with projections 0 (16:9 off) they hold
the VI's registers 0x00-0x3F, two to a word.
"""
import argparse
import os
import re
import struct

HEADER = os.path.join(os.path.dirname(__file__), '..', 'runtime', 'gc', 'gc_rt.h')
MAGIC = 0x52575452  # "RWTR"
RECORD_BYTES = 512
WHERE = {1: 'read', 2: 'pad', 3: 'ai', 4: 'ar', 5: 'jump', 6: 'beat', 7: 'fatal', 8: 'command', 9: 'leave',
         10: 'restart', 11: 'exec', 12: 'pad_si', 13: 'disc'}
PAD_SI_WHERE = 12
DISC_WHERE = 13
DISC_NAMES = ['disc_state', 'disc_stops', 'disc_dicvr']
PAD_SI_NAMES = ['si_type_1', 'si_type_2', 'si_type_3', 'si_type_4', 'sisr', 'pad_enabled_before',
                'pad_enabled_after', 'pad_raw_errs', 'pad_native']
# Counters read better in decimal; everything else (addresses, registers) in hex.
# gc_video.h's GC_VI_WIDTH_KEPT, set in the vi_mode word.
VI_WIDTH_KEPT = 0x10000
DECIMAL = {'seq', 'commands', 'reads', 'read_bytes', 'sd_requests', 'failures', 'pad_reads', 'stream_mixes',
           'stream_refills', 'ai_dmas', 'ar_dmas', 'alarms', 'beats', 'guard_hits', 'bt_events', 'bt_acl',
           'card_reads', 'card_flushes', 'card_dirty', 'vi_configures', 'projections', 'stream_starved',
           'stream_fetch_busy', 'stream_buffered', 'orthographics', 'ar_waits', 'ar_wait_longest',
           'ar_wait_timeouts', 'restarts'}


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


def record_names(names, where):
    """New pad records and disc-change records reuse crash-only words; older pad and fatal records do not."""
    overlay = {PAD_SI_WHERE: PAD_SI_NAMES, DISC_WHERE: DISC_NAMES}.get(where)
    if overlay is None:
        return names
    overlaid = list(names)
    first = names.index('crash_r1')
    overlaid[first:first + len(overlay)] = overlay
    return overlaid


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

    where_index = names.index('where')
    named_records = []
    nonzero_names = set()
    for record in records:
        labels = record_names(names, record[where_index])
        named_records.append((record, labels))
        for name, value in zip(labels, record):
            if value:
                nonzero_names.add(name)
    for record, labels in named_records:
        fields = []
        for name, value in zip(labels, record):
            if name in ('magic', 'tbu') or (not args.all and name not in nonzero_names):
                continue
            if name == 'where':
                fields.append('where=%s' % WHERE.get(value, value))
            elif name == 'sisr':
                no_response = sum(((value >> (27 - 8 * port)) & 1) << port for port in range(4))
                fields.append('sisr=0x%08x si_norep=0x%x' % (value, no_response))
            elif name == 'pad_native':
                fields.append('pad_native=0x%x si_types_known=%d' % (value & 0xF, bool(value & 0x100)))
            elif name in ('pad_enabled_before', 'pad_enabled_after') and value == 0xFFFFFFFF:
                fields.append('%s=unknown' % name)
            elif name in DECIMAL:
                fields.append('%s=%d' % (name, value))
            elif name == 'vi_mode' and value & VI_WIDTH_KEPT:
                # The picture width setting did nothing in this mode.
                fields.append('vi_mode=0x%x (width kept: the frame buffer is wider)' % value)
            else:
                fields.append('%s=0x%x' % (name, value))
        print(' '.join(fields))


if __name__ == '__main__':
    main()
