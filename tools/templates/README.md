# Dynamic ISA generation

The opcode enum, operand descriptors, and test inventory are generated from
`arkcompiler/runtime_core/isa/isa.yaml` through its upstream `gen.rb` and
`isapi.rb` at build time. These inputs are from the pinned
`OpenHarmony-v7.0-Release` runtime_core commit
`3e147bb76e0b622cc2e4399ef954b030c0639691` (Apache-2.0). Their SHA-256
digests are checked in `cmake/IsaDependency.cmake`; generated files stay in the
build tree. Ruby 3.2.3 is the pinned generator prerequisite.

Generation keeps the upstream opcode assignment, format widths, and operand bit
offsets authoritative. Adding or changing an instruction requires updating the
pinned reference and hashes, then reviewing the generated diff and the known
encoding tests. The decoder uses generated primary/prefix ordinal tables
(2,816 bytes) to select one of 332 descriptors, then reads at most 64 bits for
each of at most six operands. Its lookup is O(1); operand extraction is
O(operand bits). On this workspace's Release build, a ten-million-instruction
mixed microbenchmark measured 31.11 ns/instruction with binary search and
26.04-26.95 ns/instruction with ordinal tables. This measures decoder throughput,
not whole-file decompilation. Acceptance of a historical dynamic container
profile establishes structural decode only; historical semantic availability
needs separate rules.
# Access Inventory

`access_catalog.inc.erb` generates the immutable typed access inventory from
the same pinned ISA API. Explicit textual boundary mappings classify 146
formats into 21 families, retaining byte widths and accumulator behavior.
The generator rejects unclassified access mnemonics, duplicate mappings and
mapping entries no longer present in the ISA. Runtime lookup uses Opcode,
never mnemonic strings. Prefix stripping here is a generation boundary, not
a claim that wide/deprecated/runtime-prefixed operations share semantics.

Inventory membership does not authorize any semantic profile, lift, AST or
source publication. Actual stages must remain tied to engine route evidence.
`access_catalog_test` checks all inventory encodings and truncations;
`access_catalog_guard_test` changes only a build-directory copy of the ISA and
requires a missing classification to fail. Pinned upstream inputs are untouched.
