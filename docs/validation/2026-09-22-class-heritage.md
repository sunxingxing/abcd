# Class Heritage Validation

Debug build succeeded. Full CTest: 99/99 passed, 28.10 seconds.
ASan/UBSan class_heritage_test and class_writer_test: 2/2, 0.12 seconds.

Reference: pinned OpenHarmony-v7.0-Release runtime_stubs-inl.h,
RuntimeSetClassInheritanceRelationship. Hole selects CLASS_CONSTRUCTOR;
null selects DERIVED_CONSTRUCTOR. Initial method kind is not heritage proof.

Paired runs used only the five ABC files under /home/rabbit/sample/abc_with_code;
sidecar source was not an engine input. Fresh outputs: build/structure-heritage.
TypeScript 5.9.3 parsed all 152 declaration files and the integration verifier
checked 2111 method metadata entries and class target membership.

- 101 class sites: 32 proven absent parents, 69 unknown, zero proven null.
- Zero heritage scan failures or class site failures.
- Existing property evidence unchanged: 546 assignments, 160 own stores,
  1997 field definitions; 305 constructor assignments, 38 entry-this proofs.
- Peak RSS KiB: Immersive 8832; MultiCommunityApplication 9344;
  pasteboard 8832; push modules 8960; push widgets 8832.

These are structural evidence outputs, not complete class source. The small
paired processes do not certify the final concurrent engine's 1 GB ceiling.
No static cache or sample input was modified.

Tests cover Hole/null/undefined, copies and overwrites, unknown operation
invalidation and recovery, calls, multiple class sites, accumulator invalidation,
forward/backward edges, handler entries, malformed operands, frame bounds,
exact/short budgets including incoming edges, and 200/201/geometric sizes.
The writer test verifies PC alignment across a failed site and budget failure
degrading to unknown with an explicit error. Review found no blocking issue;
its runtime enum terminology correction is included in the constructor API.
