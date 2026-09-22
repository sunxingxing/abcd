# Initialized Captured Cells

Supersedes the capture refusal and preparation order in
`independent-leaf-functions.md`; public standalone creation remains leaf-only.

## Semantics

Extend the one-level ordinary leaf-function path to read and write parent-local
lexical cells. A child is analyzed parametrically with language-valued captured
cells. Every captured store must also be language-valued. Before publishing the
parent, its sequential plan proves that every captured cell is initialized at
the exact creation site. Parent stores already enforce the same invariant.
Thus child calls, including reentrant calls and calls after the parent returns,
observe the same mutable cells without exposing Hole/internal values.

Reference: the pinned 7.0 `ecmascript/interpreter/interpreter-inl.cpp`
DEFINEFUNC handlers (5080/5106) pass the current frame environment to
`RuntimeDefinefunc`; `ecmascript/stubs/runtime_stubs-inl.h:2525` stores that
same environment on ordinary functions. LDLEXVAR/STLEXVAR handlers (6067/6118)
walk parent environments and read/write the same slot rather than copying it.
Revision identities are in the release lock. Native JS lexical cells implement
this proven subset; debugger, GC/OOM timing and source-string identity remain
outside the existing source equivalence contract.

The first implementation conservatively requires initialization even for a
write-only captured cell. Creation before initialization, unresolved outer
chains, raw environment exposure, handlers, branches and deeper creation remain
unsupported. This is not a claim that such source programs are invalid.

## Typed Templates

Compile each unique target/length once into literal source bytes plus typed
CaptureHole entries containing byte offset and CapturedSlot. The emitter adds
these entries directly while lowering captured loads/stores; it never inserts
string markers, searches/replaces source, or infers semantics from text.
Internal template stream buffers accept capture tokens; public ordinary output
continues to reject unbound captures.

CapturedSlot is the normalized child EnvironmentFlow outer binding, not the
instruction's raw depth. All semantic captured accesses currently emit a hole,
including unused reads and write-only stores. Future text-elision optimizations
must preserve the complete proof requirements independently of emitted holes.

At each creation site resolve each hole through the parent's EnvironmentIndex
and current environment. Retain a typed LocalCell row for that site. Pass this
row in a ClosureCreation proof to the parent plan; emit the corresponding parent
cell identifier at each typed hole. Child-local cells use the reserved `$inner`
namespace, parent cells `$lex`, so nested local allocations cannot shadow the
captured cell. The namespaces concern generated identifiers only.

Native JS free-variable references preserve cell identity and lifetime. Source
template sharing never shares runtime functions, prototypes, or environments.
Parent environment allocation sites are single-execution within the supported
straight-line body, but each parent invocation has its own JS bindings.

## Ownership and Cost

Parent graph/environment storage remains live during child preparation and is
deducted first. Candidate/fact/site/fragment arrays, literal bytes, typed holes,
per-site binding rows and current child decode/analysis share the remaining
budget. Candidate scratch dies before the final parent plan. No output-sized
unbounded stringstream is used in production.

For C creation sites, U unique target/length keys and H total expanded capture
references in emitted source: sorting/resolution is O(C log C + C log M),
child analysis runs a constant number of times per unique target, mapping is
O(H log depth), parent initialization checks O(H log local_cells), and output
is linear in emitted bytes. Repeated holes deliberately retain repeated mapped
cells: O(H) storage is output-sensitive, not an unaccounted frame-by-CFG matrix.
Cross-parent caching, general loops, deeper closures and the async task service
remain separate required work.

## Implementation and Validation

1. Add a failing parent/child fixture that captures an initialized cell, then
   observes its later parent mutation; retain an uninitialized negative case.
2. Add typed captured load/store statements and ClosureCreation capture proofs
   to LinearLexicals. Validate initialization at the creation instruction.
3. Add private typed template streams and template-only source entry points;
   keep public WriteCreatedFunction capture rejection unchanged.
4. Cache child literal bytes/typed holes, map sites through parent environments,
   and stream resolved cell identifiers without source-text manipulation.
5. Test shared sibling cells, writes after parent return, independent parent
   invocations, parent/child shadowed local slots, ancestor depth, early creation
   rejection, internal writes, missing owner, budgets and geometric scaling.
6. Run Node execution, full CTest, ASan/UBSan, independent review and full corpus
   under the 1 GB address-space ceiling; commit measured results, not a claim of
   complete record decompilation.
