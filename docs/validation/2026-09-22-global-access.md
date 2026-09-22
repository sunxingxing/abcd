# Typed Global Access Checkpoint

Eight modern encodings now lift into six typed operation families, with operand
role/width checks and regional name resolution in record output. Global reads
kill accumulator identity; writes preserve it on normal completion. Receiver
proof retains explicit register aliases across both. Shared SSA/escape opcode
support is unchanged. This does not recover global declarations or complete the
instance-field, signature and body-integration gate.

Review caught a real semantic distinction: non-TRY Var IC paths can consult
global records although no-profile interpreter fallbacks use the object. The
original object-only classification was replaced with Var-family operations and
the ambiguity documented. Follow-up review found no blocking issue.

Verification:
- Debug CTest 94/94 passed, 26.95 seconds.
- ASan/UBSan global_access_test, own_receiver_test, class_writer_test: 3/3,
  0.07 seconds.
- Bad global index and bad string followed by a valid global site retain valid
  JSON and the final site's name; operand boundaries and accumulator effects
  have focused regression tests.
- Five paired files under build/structure-globals passed TypeScript 5.9.3
  declaration parsing and structural validation: 152 records, 2111 methods,
  3677 name reads, zero global site failures. Other operation families were not
  exercised by these paired files and are covered only by unit tests.
- Own writes remain 2157, with 65 entry-this proofs. Removing global-read
  barriers did not increase this count; later unsupported operations still
  prevent proof. No field/declaration completion is claimed.
- External RSS peaks: Immersive 8960 KiB; MultiCommunityApplication 9216 KiB;
  pasteboard 8704 KiB; push modules 8960 KiB; widgets 8704 KiB. These are small
  sequential structural runs, not full concurrent pipeline certification.

Input samples and static classification cache were unchanged.
