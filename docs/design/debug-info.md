# Lazy Debug Information

Continue the approved structure-first plan. Read the DebugInfo entity on
demand, not while building the global method index. Use the pinned
runtime_core `debug_data_accessor.cpp` and `debug_data_accessor-inl.h` schema:
ULEB line start, parameter count, nullable string entity offsets, constant-pool
byte length and payload, then ULEB global line-program index.

Return typed references and bounds; do not copy the constant pool or execute
the line program in this increment. Validate profile, file identity, count,
budget, pool extent, global index-table extent and resolved program reference.
Parameter strings remain lazy. Zero denotes absent name, not empty string.
Work is O(parameter count); allocated storage is explicitly budgeted.

Structural JSON gains a debug object with line start, parameter names in encoded
order, pool range and program reference. These are debug entries, not proven
source signature positions or types. Keep ABI/source signature fields distinct.
No register-local names/scopes are claimed until the line-program interpreter
validates START/END/RESTART_LOCAL and code-offset lifetimes.

Implementation sequence: failing accessor fixtures (valid, truncation, malformed
LEB, null names, bad references/index, limits and 200/201 scaling), reader,
structural serialization, paired corpus validation and sanitizer tests.
