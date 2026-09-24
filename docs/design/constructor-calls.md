# NEWOBJRANGE source recovery

Reference: pinned `OpenHarmony-v7.0-Release` ISA `newobjrange` and
`wide.newobjrange` entries and `interpreter-inl.cpp` handlers. All three
encodings carry a contiguous register window whose first entry is the
constructor. The encoded count includes that entry. The handler passes the
same constructor as `newTarget`, forwards the remaining entries in order,
and writes the accumulator only on normal completion. A zero count would
underflow the handler's count-minus-one path and is refused.

`LiftConstructorCall` validates opcode-specific widths, operand types,
the nonempty window and frame bounds. The narrow inline-cache slot is retained
as format evidence, never emitted. The register-effect pass records one compact
range read and an accumulator write; normal-return SSA marks the result as a
language object. Source output uses `new (ctor)(arg0, ...)`, preserving
constructor and argument snapshots without mutable helpers, spread iteration
or an invented receiver. The writer's two-pass output budget still applies.

Tests cover decoded narrow and wide forms, malformed operands, zero and
crossing ranges, 200/201/257/400-register windows, SSA and accumulator flow,
preflight/output, a Node Proxy construct-trap oracle (arguments, newTarget,
throwing and nonconstructible targets), and actual Immersive record export.
The latter now admits `HomeViewModel` and `RecommendViewModel`; it is not proof
that all Immersive records or historical runtime profiles are sourceable.
The dynamic 12.0.2 record writer also admits the validated decoded forms; the
paired MultiCommunity `Logger` record uses the narrow form for its
default-export instance.
The post-change source-body probe over the 900-file local corpus processed
402 dynamic files and skipped 498 classified static files with zero failures.
It admitted 1,500,536 of 5,486,254 dynamic methods, 52 more than the preceding
probe, at 178,135,040 bytes peak RSS (external `/usr/bin/time`: 173,960 KiB).
These are method-body admission counts, not a claim of whole-record behavioral
equivalence or complete dynamic-corpus decompilation.
