# Riftwii conductor review

Reviewed 2026-09-18 at commit `4d8504a`. Conductor owns review and direction; Muse owns implementation. No project code changed by this review.

## Verified baseline

- Host build succeeds; CTest passes patch, overlay, and apply (3/3).
- Parser, planner, streaming sources, single-file replacement views, and libwiigui XML browser exist.
- The end-to-end fixture uses synthetic files in host directories. It does not establish optical-disc access, game boot, or replacement reads after boot.
- No Wii disc backend, launch path, resident redirection service, or filesystem-table integration exists in the inspected project sources. Existing DOL/ELF artifacts are not evidence of hardware success. Wii build and hardware execution were not verified in this review.

## Review findings and near-term work

1. **Fix error classification before adding a disc backend.** `src/apply.cpp:100` treats every failed `open_disc` as absence when `create=true`. `DirectoryProvider` also labels every open failure as not found. Use a typed result that separates NotFound, I/O, permission, unsupported-size, and malformed-input errors. Only NotFound may create an empty original. Test both genuine absence and injected I/O failure with create enabled.
2. **Integrate ordered patches on the same file.** `plan_files` returns a sequence, but `build_replacement` handles one patch and reopens the original. There is no package executor that preserves earlier modifications. Define and verify ordering, overlaps, successive resize operations, create-then-patch, and source lifetimes. A test must consume the final file after two selected patches, not test each replacement separately.
3. **Separate runtime resource limits from file length.** `kMaxFileBytes` rejects all files over 256 MiB despite streaming. `ReadOverlay::read` allocates a request-sized scratch buffer up to 16 MiB and constructs/sorts boundaries on every call. Before runtime integration, establish measured memory and latency budgets, bounded reusable buffers, and large-file addressing. Preserve or explicitly revise failure atomicity; do not silently change it while optimizing.
4. **Restore ordinary XML compatibility deliberately.** `HasForbiddenPi` rejects XML declarations, including normal `<?xml version="1.0"?>` documents. Accept valid declarations through the XML parser while keeping an explicit DTD/entity policy. Maintain fixtures and a table distinguishing supported, unsupported, and malformed inputs. Existing tests alone establish implementation consistency, not format compatibility.
5. **Make provider preconditions enforceable.** Public provider/replacement APIs accept manually built paths but rely on planner validation. Reject traversal, embedded nulls, and invalid path syntax at the boundary, or accept a validated path type. Directory concatenation does not itself guarantee containment, especially with host symlinks.
6. **Record provenance and licensing.** There is no top-level license in the inspected root. Record exact dependency revisions, notices, asset origins, and approved behavioral references. Current vendor README identifies libwiigui 1.07; the requested libgui repository now describes a newer architecture. Document the snapshot choice without making a GUI migration the next critical task. Do not select a blanket project license without checking the actual vendored notices and obtaining the owner's license choice.

## Critical path and acceptance gates

### A. Runtime architecture decision — Muse's next primary deliverable

Produce a concrete design for original-disc boot and post-launch read redirection. Name the executing component for each step: frontend, PPC launch code, IOS-side component if used, storage access, and game-facing read path. Explain how code and data survive frontend shutdown and game memory use. Cover IOS selection/reload, request units and alignment, caches/DMA, synchronous and asynchronous completion, cancellation/errors, and storage ownership. Mark unverified assumptions explicitly.

Compare viable mechanisms briefly, select one, and identify its smallest hardware experiment. A C++ ByteSource object in frontend memory is not a persistence mechanism. Dolphin's emulator-side facilities are not a hardware backend.

Acceptance: a diagram, concrete interfaces, memory map/budget, prerequisite list, and a reproducible experiment proving the selected interception/persistence mechanism. Do not spend the next milestone expanding UI or all patch kinds before resolving this risk.

### B. Disc access and unmodified launch

Read disc identity, locate/open the game partition, parse the filesystem table with explicit endianness and bounds checks, and read a known file. Then launch the same title without patches using a documented launch sequence. Validate executable destinations against loader/service memory. Report exact hardware, IOS, game region/revision, build commit, and results.

Acceptance: known-file byte comparison plus actual unmodified game boot. Host fixtures cover malformed partition/FST structures. Emulator observations are recorded separately from hardware observations.

### C. One replacement after handoff

Connect a same-size SD replacement to the selected runtime read mechanism. First use a controlled test payload that issues reads after handoff, then a title with an observable replacement. Cover reads wholly inside, outside, and spanning the replacement, repeated reads, and error propagation.

Acceptance: replacement bytes consumed after game handoff on Wii, unchanged reads still match disc, and a visible in-game result. Frontend preview, host tests, or a boot screen alone do not pass this gate.

### D. Filesystem and compatibility expansion

Add resized and created files with correct filesystem metadata and virtual offsets; define ordered multi-file/multi-package composition. Then expand folders, parameters/options, memory patches, and save redirection in independently testable increments. Maintain a format coverage matrix; reject unsupported selected features with actionable messages. Validate semantics against public format documentation and independently authored fixtures; cite exact revisions for any permitted Dolphin behavior reference.

Acceptance: each feature has behavioral evidence, negative tests, and appropriate hardware checks. Include a file above 256 MiB and a dual-layer title before broad disc compatibility claims. Scope network support and other remaining format features explicitly before calling this a full replacement.

### E. Product completion

Connect actual disc filtering, option selection, persistence, file preflight, and launch to the GUI. Validate SD/USB scope, missing media, storage failures, repeated launches, representative titles/regions, and return/exit behavior. Add reproducible builds, distributable license/notices, and a compatibility report separating supported behavior from known limitations.

## Implementation handoff for Muse

Read this review, inspect the current code, and deliver gate A's runtime design plus the focused create/error-classification fix as separate reviewable changes. Do not assume existing host replacement tests prove console compatibility. Include regression tests for typed open failures. Propose the next small experiment with exact pass/fail criteria. Report commit, changed files, commands/results, evidence, limitations, and next gate. Keep unrelated UI work out of these changes.

Do not read, copy, translate, or adapt Riivolution source. Do not import the failed USB Loader project without an independent provenance check. Use public format/hardware documentation, original experiments, and explicitly permitted independent references. Record sources used; do not claim formally separated clean-room provenance merely because implementation text is newly written.

## References inspected

- https://github.com/dborth/libgui — current upstream describes libgui and identifies GPL licensing; local snapshot differs in layout/version.
- https://github.com/dolphin-emu/dolphin — user-approved independent project; no implementation copied in this review.
- https://wiibrew.org/wiki/Hardware/DI — hardware interface reference, including access and byte order; not a complete launch/runtime design.

Public patch-format mirror retrieval failed during this review. Passing self-authored tests is not verification of format claims.

## Follow-up behavioral reference check

Inspected Dolphin's independently authored `Source/Core/DiscIO/RiivolutionPatcher.cpp` on upstream master on 2026-09-18. Reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/DiscIO/RiivolutionPatcher.cpp . This is a moving reference, not a pinned conformance baseline; pin it before building a compatibility suite. No implementation was copied.

The existing masking, external-offset clamping, short-source zero padding, and resize formulas agree with that reference. This supports Dolphin compatibility for those rules, not verified Wii behavior. Replace comments claiming hardware certainty with the actual evidence level.

Additional gaps: Dolphin resolves relative external paths using the XML's directory; Riftwii has no XML-origin argument. Dolphin also supports filename-only disc lookup and special `main.dol` targeting, while Riftwii requires absolute disc paths. Add explicit coverage rows and original fixtures before claiming complete file-patch support. Missing external-file behavior also differs: Dolphin skips the operation; Riftwii returns an error. Decide and document whether strict preflight is a deliberate product policy.

Coordination: no Muse task was found in the available Codex task listing. `opencode` and `agy` were not available on this shell's PATH. Handoff delivery to an existing Muse session is pending identification of that session; the handoff above has not been executed.
