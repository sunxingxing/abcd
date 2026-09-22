# Receiver Calls and Constructor Evidence

The receiver-prefix proof reuses LiftReceiverCall for fixed CALLTHIS0..3.
Normal return preserves caller register identities and kills accumulator
identity. No heap property invariants or permission to emit `.call` follow.
The focused regression checks a surviving receiver alias, unknown call result
and malformed receiver range. Review found no blocking issue.

Five paired structural exports under build/structure-receiver-calls passed
TypeScript 5.9.3 declaration parsing and metadata validation. Counts remain 152
records, 2111 methods, 2157 own writes, 65 entry-this proofs and 3677 global name
reads. External RSS peaks: 9088, 9216, 8704, 8832, 8704 KiB. No broader runtime
or concurrent decompilation guarantee is inferred from these small runs.

Important direction correction: joining method inventories with class-site
constructor targets found zero own-store/define-field writes in all referenced
constructors. Runtime decoding confirms Immersive BreakpointType (7586) uses
four ordinary STOBJBYNAME assignments; WindowInfo (8371) uses eight. Own-write
counts are therefore not an instance-field completion metric. Ordinary property
assignment evidence, this provenance and class ownership must be combined next,
while preserving setter behavior and assignment placement in constructor bodies.

After global access support, debugger profiling of first unsupported operations
in MultiCommunityApplication methods with own writes found 97 fixed receiver
calls (62 CALLTHIS0, 34 CALLTHIS1, 1 CALLTHIS2) and 71 object-buffer creations,
among other barriers. Removing calls did not increase the 65-proof total.

Full regression exposed the same unlink-before-read race in module_writer_test
as prior debug fixtures. Repetition reproduced it on the seventh run. Cleanup
now happens after lazy readers; production input-identity checking is unchanged.

Final verification: Debug 94/94 in 28.42 seconds; ASan/UBSan own_receiver_test,
class_writer_test and module_writer_test 3/3 in 0.30 seconds. The repaired module
fixture passed 100 consecutive repetitions. The full source-recovery gate and
overall decompilation goal remain incomplete.
