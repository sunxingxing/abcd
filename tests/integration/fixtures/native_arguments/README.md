# Native entry arguments (LanguageSourceV6)

The user approved this separate Ark-faithful premise on 2026-10-21. These
strict module functions take one own-frame GETUNMAPPEDARGS snapshot at entry;
uses expose the same object, including through `.apply` and a named capture.
The engine receives only `entry.abc`; `entry.js` is an independent compiler/Node
oracle. No unconditional APPLY permission is introduced.

`native_arguments.mjs` compares 38 ordinary cases, including identity, missing
and extra arguments, parameter mutation, construction, receiver identity,
`callee` throwing and object brand. The sole excluded observation is Ark's own
non-configurable throwing `caller` accessor, explicitly approved by the user.

`build.json` records source/ABC and pinned SDK hashes plus the exact compile
command. `native_arguments_roundtrip.py` uses grouping_bytecode.py's compiler,
assembly reader and strict raw comparison. It applies only the existing
identifier-valued string export adaptation to both sides, preserves every diff,
and records snapshot counts independently from exact method equality.
