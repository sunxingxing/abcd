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
encoding tests. The decoder performs a binary search over generated opcode
descriptors and reads at most 64 bits for each of at most six operands. Its
worst-case work per instruction is O(log N + operand bits), where N is the ISA
opcode count. Acceptance of a historical dynamic container profile establishes
structural decode only; historical semantic availability needs separate rules.
