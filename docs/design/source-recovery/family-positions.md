# Conditional Family Position Tables

`abc_semantic INPUT.abc METHOD --family-source-draft` exports diagnostic source
and provenance, not an executable publication receipt. A successful draft still
returns exit 1 with `source_complete=false` and `executable=false`. Preparation or
printing failure does not expose partial source or position tables. An output
I/O failure can truncate the diagnostic stream; consumers must discard incomplete
JSON rather than treating a prefix as an artifact. `--conditional-family` stays
source-free. Input is ABC only; sidecar source and source maps are not consulted.

## Coordinates and Ownership

`draft_positions` has one entry per constructed family member, identified by its
creation-specific `owner` and ABC `method`. Owners must not be deduplicated by
method: separate creations can capture different cells. `begin`/`end` delimit
that member's syntax in `draft_source`; a parent's range contains its children.
All offsets are half-open UTF-8 byte positions in the **whole draft**, not
UTF-16 offsets, character counts or offsets relative to the child function.

Each entry's `positions` uses the single-function position schema:

- `value_revision` and `cfg_revision` bind the tables to the printed work tree.
- `origins` is the typed origin arena serialized at the JSON boundary. Original
  and elided sites retain their input site and PC; `original_pc`, when present,
  distinguishes the earlier coordinate from the current input PC.
- `origin_parts` holds references for combined origins. Combined ranges index
  this table and reference earlier origins; they are not copies of PC sets.
- `expressions`, `properties` and `statements` index AST nodes, each with an
  origin ID and a generated range. `anchors` retain zero-width events.
- `operations` links surviving work operations to their typed node or anchor.
- `parameters` indexes formal ordinals. `signature` links prologue operations
  migrated to formal syntax to both their original origin and formal ordinal.

Consumers join these tables, not generated strings. For example, the current
7449 draft's signature PCs 16, 20, 21, 24, 26 and 31 all refer to formal 0 at
bytes [29,41), which emits `$arg0 = null` after runtime-name recovery. This does not mean the removed
prologue was a pure-effect erasure: its independent cell-default proof and
initialized-source-parameter residual remain prerequisites.

## Bounds and Limitations

The shared serializer visits each table once and allocates only one small JSON
entry at a time. It does not flatten the origin DAG for each AST node. Thus
serialization is O(table elements + serialized bytes), with bounded per-entry
scratch in addition to the already-admitted tree and printer events. The draft
source sink has a separate 1 MiB diagnostic limit and discards failed output.
Whole-process 1G compliance remains unmeasured while RSS work is paused.

These tables describe the emitted conditional tree. They do not independently
prove all decoded PCs are covered, authorize a lexical self name or root enclosure,
close module/global/strictness dependencies, or grant executable equivalence.
Tests for owner ranges, signature PCs and the source-free selector are written
in `tests/integration/family_source_cli.cmake` but remain unrun under the testing
pause. No new source evidence-rule version is claimed for this diagnostic view.
