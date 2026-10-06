# Debug-backed formal names through entry identity

Rules 131 replaces the entry MOV prefix and fixed captured-this sequence in
`MapParameterDebugNames`. Source spelling is a metadata hint; formal identity
comes from the actual original parameter slot at method entry. The same resolver
serves normal/phased families, class methods and the older callable adapters.
It does not recover parameter types from debug strings or read paired source.

## Identifier domains (rules 133)

The private typed ParameterNameDomain separates the legacy writer's reserved
textual namespace from the common tree's source identifier domain. The shared
ResolveFunctionSyntax selects the existing IsSourceIdentifier predicate; older
callers retain the legacy default. Legal dollar names and ClassValue/MethodSelf
prefixes can therefore retain actual debug-backed formal names. These prefixes
are writer conventions, not language restrictions. Reserved words and invalid
identifiers remain rejected by the source predicate. The common family reference
planner still avoids real descendant global/module shadowing, and original
scope, enclosure, ABI, entry identity and duplicate-name obligations remain.

Eighty additional encoded DebugInfo/DebugScopes fixtures vary ten formal hints,
global/module reads, named/plain creation and straight/split control. The total
128 families pass 2752 independent behavior checks and the existing 48 valid-JS
mutations. Tests verify original metadata evidence, generated formal spelling,
real free-reference conflicts and the legacy default on the same decoded input.
The domain switch adds no algorithm, dependency or new asymptotic cost. The
existing restricted ASCII identifier set remains; Unicode expansion, missing
formal evidence, types, rest/default syntax and whole-record acceptance are
separate open work. No paired source supplies production naming facts.

Every valid register debug-range start becomes an analysis-only observation
before that original instruction. Normal CFG validation, pinned semantic lifting
and exception dispatch supply the Before/Commit/Catch topology. Queries have no
write or language effect. The existing `RegisterSsa::BuildSites` computes their
values along all original paths; results remain in this private analysis and do
not alter ValueProgram, AST instruction ownership, code motion or printed bodies.
Catch defines the exception accumulator; a throwing normal destination commits
only on normal completion. A normal branch to handler code bypasses Catch.

Raw suspension and operations lacking known semantic effects are explicitly
opaque in this observation adapter, never fabricated NoOperation instructions.
Their possible normal and selected handler paths invalidate subsequent frame
identity facts. Reachability and a sparse forward worklist keep earlier queries
valid unless an actual backedge/handler can carry unknown state back to them.
Dead opaque instructions provide no facts and poison no reachable observation.
This bounded uncertainty allows independently known entry names without claiming
complete semantics for an unsupported tail. Observations beyond such a boundary
remain unnamed; this is an implementation coverage gap.

A bottom/one-entry-parameter/other agreement lattice propagates through typed
transparent scalar copies and actual phi inputs. Mixed entry parameters,
computed values, exceptions and ungrounded cycles cannot establish a name.
An original ABI-slot write is handled by this same def-use graph, rather than a
blanket rejection of prologue writes. Direct ABI ranges and copies through
accumulator/registers use the same entry facts. Non-boundary/empty ranges provide
no naming evidence. The caller still requires evidence for every formal, validates
strict ASCII identifiers and duplicates, and checks enclosing name visibility.
No partial debug-name set is yet admitted by the existing caller.

A debug local can begin before its initializer, while its register temporarily
holds a copy of a parameter. Such a downstream local must not replace an already
named upstream storage. Iterative Kosaraju condenses the transparent copy/phi
graph; candidate names in ancestor components take precedence over descendants.
This uses dependencies, not PCs, register numbers, copy distance or graph shape.
Independent roots and conflicting names in one component remain ambiguous.
A single named copy cycle stays valid and cannot become its own conflicting
ancestor. This is presentation precedence, not a new source-storage certificate.

With I original instructions, H handlers, Q observations, V SSA values and E
copy/phi dependencies, the new owner retains O(I+H+Q+V+E) storage. Boundary lookup
and observation sorting cost O(Q log I + Q log Q); per-instruction dispatch lookup
and the common CFG/SSA costs are separate. Agreement, uncertainty propagation,
SCC condensation and name precedence cost O(I+H+V+E), plus actual query reads.
Each agreement state changes at most twice. There is no per-formal graph walk or
dense instruction/register snapshot matrix. Vector capacities and simultaneous
scratch are admitted under the existing byte budget. Whole-process RSS and
latency benchmarks remain paused; these bounds are not an RSS compliance claim.

Provenance is standard sparse SSA/copy propagation, finite-height worklists and
Kosaraju SCC decomposition, implemented locally using the existing pinned ISA
adapters and standard library. No third-party code or new dependencies/licenses
are introduced. The adapter must track upstream register-effect/dispatch changes;
opaque facts must never certify source recovery or erase body effects.

Decoded fixtures cover MOV/LDA/STA, NOP/split blocks, direct ABI naming, same/mixed
phi origins, loops/self-copy cycles, preserved registers on throw, catch writes,
raw suspension before/after observation and backward routing, dead unknown tails,
conflicting/duplicate hints and named ancestor copies. 200/201/402/804 formal
observations and the existing copy-growth cases exercise sparse scaling and byte
limits. The independent Content method test generates only from ABC, then loads
paired TS for 176 length/coercion/exception/fresh-object scenarios and detects a
valid-JS mutation dropping count. This verifies that method, not a platform UI
or whole-record acceptance gate. Current build/output evidence is in status.md.
