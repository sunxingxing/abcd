# Member Base Printing

The shared printer uses one Member routine for named reads, member calls,
ordinary stores and independently admitted own stores. Accumulator/register
base values render as validated identifiers or typed native `this`, including
SSA-proven immutable entry aliases. These primary expressions need no grouping
before a member key. Every other ScalarValue retains its existing grouping,
including numeric literals with lexical ambiguity before a dot.

This change selects syntax from typed operands, never printed text. It does not
change member-key classification, evaluation order, assignment admission,
getter/setter effects, or the Reference receiver of a member call. Work is
constant per member operation and uses no additional retained storage. Printer
positions continue to record actual output coordinates.

Both production targets built successfully. ABC-only export
`build/pasteboard-member-bases-01` emits `this.sm = sm` and `return this.sm`
in BreakpointType, and the same rule applies across the shared output path.
Eight incomplete app records, two constructor refusals at PC14 and nine metadata
records remain. Method maps retain 195 entities / 9,745 spans. Read-only review
found no concrete issue. Tests, generated JS execution and RSS remain paused;
numeric/quoted keys, receiver-sensitive calls and final-position regressions
are deferred. This does not establish complete runtime equivalence.
