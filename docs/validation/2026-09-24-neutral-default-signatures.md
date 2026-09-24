# Neutral Default Signature Marker

Pinned v7 `DEFINEFUNC` and class-buffer lengths are runtime-visible function
`.length` values, while the decoded frame separately reports formal slots.
An exact `COPYRESTARGS` prologue continues to recover rest syntax. For a
smaller creation length without any rest-copy instruction, the writer now
places `= void 0` on the first formal at that length and leaves the method
body unchanged. The initializer is pure and preserves the ABI input value;
the original bytecode body still evaluates any real default value. The
writer refuses getters/setters, impossible lengths, malformed rest prologues
and unproved historical profiles. This path scans the body once and adds
constant scratch state.

Unit and Node oracles cover a real undefined-check/assign-null bytecode
branch, omitted versus explicit arguments, a marker after the first formal,
ordinary/async/arrow functions, class methods and constructors, function
length, construction and arrow nonconstructibility. All 163 CTests passed.

The paired pasteboard `PageABuilder` method entity 7449 has one formal slot
and creation length zero. It now passes signature lowering, but the real
method still fails body emission at PC 75 with
`BodyEmitError::kUnsupportedInstruction`. Its record entity 6897 remains
unpublished at entry PC 6, `RecordProgramError::kFunction`. A single-file
`/usr/bin/time` measured 10,752 KiB (11,010,048 bytes) peak RSS for a fresh
`abc_structure --source` pasteboard process, below the 1,000,000,000-byte
bound. Direct shell-exec invocations can inherit a much larger `ru_maxrss`
high-water mark from the launcher; the earlier reported 466,903,040 bytes
was not a clean-process measurement. This paired-file run does not certify
the full corpus.
