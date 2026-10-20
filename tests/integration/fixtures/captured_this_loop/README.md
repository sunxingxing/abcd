# Captured receiver stored before a creator loop

`build.json` records the pinned DevEco es2abc identity, O0 command and hashes.
The creator stores `this` in one environment cell, then creates read/write
closures inside a loop. It never loads that cell itself. That distinction is
intentional: OLD at 2a0ca2a8 refuses ScopeError::kInitialization; the shared
completed-store dominance index now supplies the missing capture fact.

The Node differential runs both contracts, zero/one/multiple iterations,
break/throw exits, receiver mutation and writes through retained closures.
Each call retains its own activation. The wrong-cell mutation changes the
observed results. Original sources are evaluation-only, never engine inputs.

SDK round-trip differences are recorded by staged_field_roundtrip.py without
normalization. Source temporary/phi control and recovered binding spellings
may change register/cell naming and Boolean branch lowering. The semantic
proof and Node trace comparison are distinct from bytecode identity.
