# V7 object rest: excluded keys and native pattern correspondence

The 2026-10-22 user decision extends the existing V7 `ObjectRests()` predicate:
Ark uses strict SetProperty rather than CreateDataProperty, and unwraps a Proxy
source before copying. These are the only accepted differences. No new contract
version or arbitrary excluded-key admission follows.

Pinned execution: `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:4149`
and `:4164` read the source and compact excluded-key window. The encoded count
is one less than the window size. `stubs/runtime_stubs-inl.h:2565` returns a fresh
object, returns empty for nullish input, compares excluded keys with abstract
Equal, and gets/sets remaining enumerable properties. `:2624` treats an Undefined
first key as the empty exclusion list. `:2642-2648` unwraps the Proxy.
Consequently native rest also needs the separately executed coercibility guard;
raw CREATEOBJECTWITHEXCLUDEDKEYS is not intrinsically destructuring. Non-string
keys without a proven canonical property-key domain remain refused: e.g. raw
numeric 1 can abstractly exclude the distinct property string "01".

`ObjectRestPlan` derives the source identity and ordered Get commits from the
original SSA graph. One shared edge-split dominator index proves non-null and
non-undefined on every incoming path. One shared effect-order index establishes
exact consecutive observable Gets followed by the copy, across pure CFG splits.
A canonical-use index rejects any use of an extracted value before the copy,
including uses through SSA copies. Each Get and copy must retain its real
exception exit and the same handler identity. The verifier recomputes all facts.
The register window stays compact until an admitted source key table is needed.
All indexes and output fields are charged before allocation; total admitted
work is O((V+E) log V + U), not a graph rescan per rest construction.

The source surface consumes existing result commits into a native assignment
pattern. It neither repeats a Get nor replaces the rest copy with a helper or
ordinary assignment. The existing source-temporary declarations remain. The
original explicit coercibility guard also remains; the pattern's additional
coercibility test is inert after that guard. These extra checks and conservative
source temporaries can change recompiled instructions, so the exact round-trip
comparison is recorded separately from Node/event equivalence. Recovery keeps
these occurrences unchanged and runs the common joint flow/value/exception
solver. The printer independently checks the retained surface; both sinks use
it. Get origins move to their pattern properties, with textless source anchors
retaining boundary spans.

The pinned frontend has an additional, **unapproved** discrepancy:
`ets_frontend/es2panda/compiler/base/destructuring.cpp:192-211` loads an identifier
key's *spelling* for the exclusion list, even for `[k]`, then evaluates `k` for the
Get; non-identifier computed expressions can be evaluated twice. A fixture
`const {[k]:v,...rest}=o` must therefore refuse unless the actual key identities
are proved equal. Effectful double evaluation is not dropped. The positive SDK
fixtures include static names and computed string constants. Constructed dynamic
String snapshots use `[(0, snapshot)]`: the frontend then copies the value, not
the identifier spelling. The two inert reads retain one key-producing event.
The snapshot is private, uncaptured and resolved at the pattern scope, and its
producer remains a statement; snapshot fusion into an omitted Get is forbidden.
Recompiling and re-certifying that emitted pattern is mandatory in the test.
Actual dynamic mismatches and twice-evaluated effects are refusal tests. This is not a new premise or a source-shape rule.

Exact fixture round-trip report: all ten registered inputs recompile at default
and O0 (twenty comparisons, both sides compiled), with no syntax failures. The strict
`grouping_bytecode.py` comparison reports differences, not passes: the retained
null/undefined guard uses the existing native logical decision spelling; the
error arm's empty-pattern TypeError lowers to its own guard and an unreachable
outer throw; the native rest pattern adds its coercibility guard. These account
for the extra Boolean conversions, branches, throws and shifted IC-slot counts.
Conservative private key/result temporaries can add register moves. The ordered
Get keys, one fresh rest-copy operation, extracted values, result assembly and
caller-visible exception routing are independently certified and tested. The
ordinary grouping token checker also correctly reports a structural change;
it is not a semantic oracle for a destructuring recovery transformation. No new
bytecode normalization or permissive comparison mode was added.

The strict whole-record result is **0 exact / 10 different / 0 unverified** in
each compiler mode. This is source recovery, not a parentheses-only rewrite.
The module prologues can also differ through function-declaration versus retained
creation-binding emission and export ordering. Large WIDE source uses private
key/result registers; the count stays 260, rather than accepting a one-key WIDE
encoding as evidence for the large key window. Dynamic reference-source
recompilation has the documented frontend identifier-key bug; it is not counted
as equivalent to the constructed input ABC. Instead the emitted value-sequence
pattern is independently re-certified and Node-tested after default **and** O0
recompilation, establishing the actual exclusion/Get key identity on that path.
The occurrence writer leaves unmatched round-trip events null.

The certificate intentionally rejects a numeric/object exclusion domain, a key
whose Get and exclusion identities disagree, effectful duplicate key evaluation,
a missing original guard/exception exit, an early observer, or a changed handler.
Recovery retains the original expressions/definitions; the printer verifies
private uncaptured target storage and lexical name resolution before consuming
the pattern. No user-visible target evaluation is moved into the pattern.
