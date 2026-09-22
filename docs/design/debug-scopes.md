# Debug Local Ranges and Source Positions

Continue structure-first work using pinned runtime_core file_items.h and
line_number_program.h. Two bounded MetadataCursors read the opcode stream and
debug constant pool independently. Signed register operands are in the opcode
stream; names/types, pc/line increments and source references are in the pool.

Return typed source positions and debug local ranges. Keep signed debug register
numbers unchanged: they are not yet mapped to virtual-register/source bindings.
Local end offsets are optional and set only by an explicit END_LOCAL. Never
imitate DebugInfoExtractor's zero sentinel or close every variable at the last
line-row address; that would conflate a real zero-length range and missing end.
Retain terminal debug PC separately. Do not emit let/const based on these alone.
Retain terminal file/source references even when no later line/column row exists.
Dynamic emitter INVALID_LINE=-1 is encoded as UINT32_MAX: use a signed line
accumulator, publish this sentinel as typed absence, and permit subsequent line
deltas to recover a valid line. Other negative results and overflow are errors.

Reject RESTART_LOCAL explicitly (unsupported upstream), unknown END_LOCAL,
overlapping START_LOCAL on an active register, invalid references, pc beyond
code size, signed line overflow, truncation and resource exhaustion. Keep
uninterpretable debug status separate from known record/module structure.
Markers are consumed as schema no-ops; this is not an emulation of the upstream
processor's handler-return-value bug on leading prologue/epilogue markers.

Use a register-to-local-index ordered map, O(E log R) work for E debug events
and R distinct registers; no backward search over accumulated locals. Charge
map nodes and vector capacities (including transient vector growth), with a
separate event-count budget. Strings remain lazy references. The CLI process
ceiling continues to cover allocator bookkeeping outside these allowances.

Tests first: separate operand streams, negative registers, extended names/types,
explicit zero endpoint versus absent endpoint, repeated register lifetimes,
special position math, malformed references/LEB, truncated pool/program,
unsupported restart, budgets, and geometric 200/201/8192 event scaling. Then
integrate JSON evidence, rerun paired samples and sanitizer tests, review/commit.
