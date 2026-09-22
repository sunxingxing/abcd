# Ordinary Property Assignment Recovery

PropertyWrite now unifies indexing of ordinary STOBJBYNAME assignment, own store
and field definition without equating their runtime effects. Receiver proof
and structural output consume the same typed lift. Related APIs/files were
renamed from own_receiver/own_writer to property_receiver/property_writer to
reflect the actual domain; low-level OwnWrite/NamedWrite remain distinct.

Five paired ABCs under build/structure-assignments passed TypeScript 5.9.3
declaration parsing and structural validation: 152 records, 2111 methods,
546 ordinary assignments, 160 own stores, 1997 field definitions, zero write
site errors. Entry-this proofs total 103. Joining class-site constructor targets
to their methods identifies 305 ordinary assignments in constructors, 34 with
proven entry-this receivers. The join is validation evidence, not yet a typed
engine class-instance relation. No source fields or bodies were emitted.

External peak RSS: Immersive 8960 KiB; MultiCommunityApplication 9344 KiB;
pasteboard 8832 KiB; push modules 8960 KiB; widgets 8704 KiB. These sequential
structural checks do not establish complete concurrent pipeline compliance.

Review found no blocking issue. Requested tests added: both STOBJ encodings,
all three write kinds, width/role/range error propagation, and mixed own and
ordinary writes with a bad-name site between different receiver identities.
ASan/UBSan property_write_test, property_receiver_test and class_writer_test:
3/3 passed in 0.07 seconds. Input samples/static classification cache unchanged.
Final Debug regression: 95/95 passed in 27.23 seconds; diff whitespace check
passed.

Remaining gate: extend receiver proof through module loads, constructor/super
state and control-flow joins; connect class ownership; retain setter-capable
assignments in their original body order; recover signatures and global binding
declarations; integrate actual source bodies. Metadata is not completion.
