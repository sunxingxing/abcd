# Frame This Identity

LDTHIS reads frame.thisObj in both pinned interpreter implementations, not the
optional writable ABI this register. The entry-prefix analysis now preserves
that distinction. Tests cover absent/overwritten ABI slots. Unsupported super,
resume and control joins still stop proof; no instance declarations are added.

Debug: 93/93 passed (26.61 seconds). ASan/UBSan own_receiver_test and
class_writer_test: 2/2 passed. Review found no blocking issue.

Five paired ABCs passed structural validation under build/structure-frame-this:
152 records, 2111 methods, 2157 writes. Entry-this proofs remain 65; no sample
coverage gain is claimed. External RSS peaks: 8960, 9344, 8704, 8832, 8832 KiB.

Debugger profiling of first unsupported operations in MultiCommunityApplication
methods containing own writes identified TRYLDGLOBALBYNAME_IMM8_ID16 in 183
methods, CREATEOBJECTWITHBUFFER in 14, CREATEEMPTYARRAY (narrow/wide) in 7,
LDEXTERNALMODULEVAR in 5, CALLTHIS1 in 3 and CREATEARRAYWITHBUFFER in 1.
These are method stop counts, not write counts. This evidence prioritizes typed
global accesses next, covering both global-variable recovery and receiver flow.
