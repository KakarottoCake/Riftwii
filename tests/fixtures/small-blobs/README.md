# Small-blob differential references

These are RiftWii's own sources from commit
`4b1d99ee1258e59858b00d17250b00aafec8c66f`, before this rewrite.
Keep the algorithms frozen: tests compare the new code against them.

Public helper names have a `legacy_` prefix so both versions can be linked.
The hook sources have the same narrow host seams as the current sources:
CPU/cache operations and IOS calls are mocked. The screenshot capture itself
is omitted in the host job model; its report parser, frame description and
header bytes are compared separately. No assembly is executed by these tests.
The production build's two-address checks still cover position independence.

`smallblob_tests` compares reports, filenames, attributes, screenshot bytes,
crash bytes, synchronous crash-save requests, asynchronous screenshot jobs,
BCA replies, and the return stub's IPC requests. Both success and failure
paths run. The low-memory portions need a mapped region at `0x91000000`;
exit 77 means that reservation was unavailable.
