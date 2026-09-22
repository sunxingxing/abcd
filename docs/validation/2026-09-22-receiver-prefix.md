# Entry Receiver Copy Proof

The current gate (instance fields, globals, source signatures and body
integration) remains incomplete. This change adds a bounded identity proof,
not class field declarations or completed record source.

`ProveOwnReceivers` tracks typed entry-this aliases through scalar copies and
reviewed normal register effects. It pre-scans all branch and handler targets,
stops before the earliest incoming target, and stops at non-fallthrough or
unsupported operations. Later sites are explicitly unknown. It does not infer
objectness from a derived constructor's entry value or treat super as a NOP.
The shared register-effect whitelist is unchanged.

Tests cover LDA/STA/MOV, self-copy, original-slot overwrite with a surviving
copy, ordinary call preservation, a decoded backedge, handlers, unknown effects,
missing this, frame bounds, exact/short budgets and register sizes 200, 201,
400, 800, 1600 and 40000. Review found no blocking issue; requested cases were
added. The analysis uses O(N+R) time and O(R+W) extra storage with a 16 MiB
default configurable receiver budget.

Five paired ABCs under `build/structure-receivers/` passed TypeScript 5.9.3
declaration parsing and structural JSON validation: 152 records, 2111 methods,
2157 own-write sites. Of these, 65 have proven entry-this identity; all retain
`instance: null`, since instance and class placement are not yet proven.

External peak RSS was 8960, 9216, 8704, 8960 and 8832 KiB respectively for
Immersive, MultiCommunityApplication, pasteboard, push modules and widgets.
These bounded sequential checks do not certify the future complete concurrent
decompiler. Samples and static classification cache were unchanged.

Full regression exposed an existing temporary-file lifecycle race in
debug_scopes_test: unlink after Open but before Read changed ctime and sometimes
produced FileChanged. The test now unlinks after reading; 100 consecutive repeats
passed. The touched class-writer fixture receives the same cleanup ordering.
Production file-identity checks are unchanged.

Final Debug regression: 93/93 passed in 27.14 seconds. ASan/UBSan targeted
regression: own_receiver_test, class_writer_test and debug_scopes_test, 3/3
passed in 0.17 seconds. `git diff --check` passed.
