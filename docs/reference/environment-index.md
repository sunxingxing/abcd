# Lexical Environment Ancestry

`EnvironmentIndex` resolves lexical depth/slot addresses without confusing
separate allocations or walking an entire parent chain at every use. It is an
immutable binding-analysis primitive, not a JS variable-name generator, closure
emitter or whole-method control-flow analysis.

## Identity Contract

The caller supplies definitions in parent-before-child order. Each input position
is a distinct `EnvironmentId`, even when two definitions have identical parents,
slot counts and scope literals. Local parents must refer to earlier positions;
self-links, future links and out-of-range links fail before allocation.

`CapturedDepth{0}` denotes the method-entry ordinary lexical environment.
Larger values denote its ancestors; they are unresolved capture identities,
not local environment IDs or source variable names. All references belong to one
capture context and one index. IDs cannot be compared across indexes without
an explicit capture-context mapping. Shared/sendable environments are separate.
This is a caller precondition, not runtime ownership validation. Like register
and block IDs, these addresses are context-relative rather than capabilities.

`Resolve(current, address)` selects the exact ancestor and preserves the slot.
Known local allocations check `slot < allocation.slots`, including empty scopes.
Captured slot bounds remain unknown, not validated. Named allocations retain
their regional scope-literal pool slot through `Find`; this does not resolve
source names or change binding identity.

The pinned interpreter's NEWLEXENV variants at `interpreter-inl.cpp:4583`
through 4639 install a new child as current; POPLEXENV variants at 4648-4660
restore its parent. Loads/stores at 6067-6231 walk precisely the encoded number
of parents. This index models that ancestry, not value/accumulator effects;
those remain in `LiftLexicalInstruction`. New slots still contain Hole.

An allocation site is not a unique runtime object. When analyzing loops or
multiple closure creation occurrences, preserve dynamic environment freshness.
The index does not resolve CFG environment joins or permit choosing an arbitrary
parent when incoming environments differ. Captures may outlive a pop; old IDs
remain valid because definitions are never removed or mutated.

## Algorithm and Resources

The jump-pointer technique is described in section 3.2 of Bender and Farach-Colton,
[The Level Ancestor Problem simplified](https://www3.cs.stonybrook.edu/~bender/newpub/2004-level.pdf),
TCS 321 (2004), DOI 10.1016/j.tcs.2003.05.002. We use an original implementation
of bounded doubling, not copied code or the paper's more elaborate optimal
macro/micro-tree implementation. With the ISA's 16-bit distance, at most 16
table levels are needed; this avoids extra ladder/decomposition machinery while
keeping both memory and query work explicitly bounded. No new dependency or
third-party code license is introduced.

For a local node, level k stores its local ancestor at distance 2^k. Level k
is obtained by composing two level k-1 jumps through an already built parent.
Typed absence means that jump crosses into the captured chain. Each node also
stores its distance to that chain and the chain's entry depth. Queries crossing
the boundary use checked arithmetic; queries staying local follow the set bits
of the requested distance. No recursion or mutable caches are used.

Captured arithmetic is deferred until query time. A definition whose parent is
`CapturedDepth{UINT32_MAX}` is still valid for local or immediate-parent queries;
only a query beyond that representable depth fails. Precomputing every captured
jump must not cause spurious construction failures.

For N definitions and D=65,536 depth values, construction/storage are
O(N log D), query is O(log D), and slot validation is O(1) after ancestor lookup.
Thus the fixed-width implementation has linear construction and at most 16
query iterations. It has no all-pairs table or per-variable whole-tree walk.

The fixed node-array payload is admitted before allocation and reported by
`AllocatedBytes`. The default budget is 64 MiB; exact and one-byte-short budgets
are tested. Input definitions and allocator overhead belong to caller admission
under the whole-process 1,000,000,000-byte limit. Allocation failure is typed.
The index is move-only; moved-from instances expose no local identities.
Const queries can share a completed index without synchronization; concurrent
move/destruction is not allowed. This is not the future async task scheduler.

## Verification and Next Integration

Tests cover sibling allocations, pop-equivalent parent queries, shared ancestors,
external capture anchors, retained scope IDs, empty slots, invalid IDs/parents,
depth overflow, empty indexes, move construction/assignment and exact resource
boundaries. Thirty-two
seeded 64-node forests are checked against an independent parent-walking oracle.
Chains at 1/200/201/4,096/65,537 nodes query the maximum encoded distance for every
node and verify linear payload growth.

Debug CTest passed 52/52; the new index test also passed ASan/UBSan. Running
`/usr/bin/time -v build/decode/environment_index_test` completed in 0.10 seconds
with peak RSS 14,983,168 bytes. This is a synthetic index test measurement, not
the memory cost or semantic validation of full-corpus source recovery.

The [environment-flow analysis](environment-flow.md) now derives definitions and
incoming states from real method CFGs, accepting matching joins and refusing
conflicting ones. The next integration must connect function creation to the
correct captured context and preserve Hole/TDZ checks before emitting source
bindings. Exception/resume flow and conflicting joins remain unresolved.
No sample method becomes source-ready solely by constructing this index.
Full record recovery remains required.
