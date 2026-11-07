This is RiftWii's own FAT and request-adapter implementation from
`4b1d99ee1258e59858b00d17250b00aafec8c66f`, preserved unchanged as a test oracle.
It is built only for host tests. Do not update it when changing the runtime.

The same test drivers run against this copy and the current implementation.
Their transcripts compare every device transfer (including failures and bytes),
results, metadata, file positions, directory cursors and whole-image fingerprints.
The request tests also compare classification while another operation is active.
