# Acyclic Accumulator Initialization

`AnalyzeAcyclicAccumulator` establishes whether supported instructions read
only language-valued accumulator contents on entry-reachable normal paths.
It takes a validated method and its unchanged `BuildNormalFlow` graph, plus
the frame size. Entry registers must contain language values under the caller's
ABI contract. This is not a verifier for arbitrary forged graph/body pairs.
Block instruction partitions are nevertheless checked before indexing.

The lattice has three typed states: unreachable, may-be-Hole, and value.
Entry acc may be Hole. A successful supported scalar/string load writes a
value; `ldhole` kills that fact. Register destinations preserve acc. Reads of
acc require value state. A join keeps value only when every incoming reachable
normal edge supplies value. The merge is order-independent, including duplicate
edges. No constant-branch inference removes paths. `ldhole` is modeled here
for initialization analysis, not declared valid for JS emission.

The pass uses the existing bounded acyclic block order and visits each reachable
instruction once. Typed scalar-expression access inspection is shared with the
straight-line writer rather than inferred from printed source or ISA flags.
Supported branch predicates preserve acc and require a language value. Modern
named loads consume acc and produce a value; constant string loads produce a
value but their pool contents are not resolved by this analysis.

Time is O(I+B+E) for supported bounded instruction shapes; no per-register or
per-predecessor whole-function rescans are used. State storage is O(B), allocated
before the ordering pass so its budget includes simultaneous ordering scratch.
`allocated_bytes` includes vector capacities, not allocator bookkeeping or the
caller-owned graph/body. `instruction_visits` records actual visited instructions.
The overall process still requires independent peak-memory enforcement.

Exception regions, resume edges and reachable loops are rejected explicitly.
Unhandled operations fail at their originating PC; successful analysis does
not prove that every instruction has an emitter, constants are valid, or whole
function semantics have been reconstructed. Unreachable entries are retained
as such, not erased. This analysis is groundwork for a structured acyclic source
path; loop, exception, lexical binding and full per-record recovery remain open.

Tests cover diamonds with and without a Hole-producing arm, reversed successor
order, skipped initialization, a normally unreachable Hole write, overwriting
Hole from a register, unsupported operations, malformed partitions, exception
and resume rejection, reachable cycles, zero/exact/short budgets and
200/201/4096/65536-branch chains with one instruction visit per reachable
instruction. Shared access inspection is also covered by existing scalar and
straight-line source tests.
