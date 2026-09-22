# Module Load Receiver Flow

Four modern local/external module-load encodings now lift into typed module
space and slot records. The property-receiver proof clears accumulator identity
and preserves private caller register identities on normal completion. It does
not infer JS value type, purity or initialized state, resolve a slot as a request
ID, or remove Hole guards. Shared SSA/escape instruction coverage is unchanged.

Reference: pinned interpreter-inl.cpp module handlers 6304-6341, SlowRuntimeStub
module-load wrappers and runtime module accessors. Review found no blocking
issue, including external access paths that may invoke property reads.

Five paired files under build/structure-module-loads passed TypeScript 5.9.3
declaration parsing and structural validation. Counts remain 152 records and
2111 methods. Receiver proofs increased from 103 to 109, including constructor
assignments from 34 to 38 out of 305. These are identity proofs, not completed
instance field declarations or executable source files.

External RSS peaks in KiB: Immersive 9088; MultiCommunityApplication 9344;
pasteboard 8704; push modules 8960; widgets 8704. These small sequential runs
do not certify the complete concurrent decompiler. Static cache and inputs
were unchanged. ASan/UBSan module_load_test and property_receiver_test passed
2/2. Tests cover all four encodings, operand width/role/count validation and
unknown module result versus surviving receiver alias.

Final Debug CTest: 96/96 passed in 27.02 seconds. Targeted ASan/UBSan elapsed
0.03 seconds. Diff whitespace check passed. Full source-recovery gate remains
incomplete.
