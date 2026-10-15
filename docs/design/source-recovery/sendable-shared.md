# Sendable classes and shared modules

## Accepted output policy and staging

2026-10-15: V3 authorizes ArkTS-preserving output, never erasure. The approved
four increments are (I) metadata/evidence, (II) closed Sendable class projection,
(III) Sendable module reads/member environments, (IV) shared-module publication.
Generic field types require a binding-backed spelling that recompiles to the
same mask; otherwise refuse. Each publication requires independent tree and
original/recovery correspondence plus actual output recompilation.

## Round 74 increment I

`abc::ClassKind` is preserved by raw class lifting and `ValueClassCreation`.
The original creation chooses the literal layout: ordinary ends in the member
split; Sendable additionally ends in a direct field-literal EntityId. The member
buffer and field buffer are separate identities. Field order, masks (including
NONE/GENERIC), and instance/static split are preserved without inventing types.
`ReadSendableMetadata` re-decodes the original occurrence, resolves that method's
regional pool, checks constructor/member record owners, then reads the direct
field reference. Key entities are sorted/deduplicated before string validation;
aliases do not cause repeated long-string scans. For one diagnostic occurrence, N counts original method instructions, referenced
literal entries and distinct key units: work is O(N log N), storage O(N), with
byte admission before allocations. Increment II's batch preparation must share
original body/literal indexes; it must not call this single-refusal wrapper once
per creation and repeatedly decode the whole creator. Nested readers receive an exclusive
remaining-budget loan; caller transfers their actual storage only after return.
Released nested string/boundary scratch can make the exact admission threshold
larger than the cumulative retained charge; tests check the real threshold and
threshold minus one, not a fabricated equality.

The shared record attributes (absent differs from false) remain bound to the
same file/record as the borrowed immutable module table. They do not make a
shared module `OrdinarySynchronous`, and do not bypass ModuleCapturePrefix,
cleanup or lazy module prerequisites.

Fixed-size SendableFailure / SharedModuleFailure witnesses are serialized in
route receipts, the two record failure JSON paths, and abc_semantic's record
view. Sendable locations distinguish original PC from working PC; the raw
metadata reader does not invent a normalized PC. Failure decoration runs only
on an actual refused occurrence. Existing first causes and resource classes
are unchanged. No symbols, owners, or names are allocated by a metadata proof.

At increment I, production gates remained: semantic lifting refused Sendable source; the source
class-site index remains ordinary-only; shared entry remains kModule/kShared;
both V3 policy predicates were disabled. The new Value kind is groundwork,
not permission to recover an ordinary lexical environment for a Sendable class.

## Target evidence

- Sendable record 28934: creator 29066@9, constructor 29121, class literal29308,
  fields29289 = taskId/NUMBER(1), instance count1; no imports, local export slot0.
- LruCache record24892: isSharedModule=true, module25407, request0
  @ohos:arkts.utils/default ArkTSUtils, local export slot0 lruCache. Its class
  creator25192@12 targets25220, class25573, fields25544 = lock_/GENERIC(32),
  books_/GENERIC(32), instance count2. The two reads25220@13/@51 are Sendable
  external module reads, not empty sendable environment allocations.

The next increment must retain both constructor [[Set]] writes for the closed
class, emit initializer-free TS field descriptions and a verified constructor
directive, then compare actual exported output recompiled by DevEco. No such
publication or round-trip is claimed by increment I.

## Increment I acceptance

Base544e0854, no commit/push/stash. Full keep-going build succeeded; fixed65/65;
new/directly changed tests5/5; targeted24/25 (pre-existing class_tree_source_test
anonymous-class regex); sweep69/70 (pre-existing family_source_cli_test).
Exploration vs explore73c/explore73cs:521/620 and565/620, no status or source/
extension drift, complete structural artifacts unchanged. Each manifest set
changes only the two target witnesses; first causes stay unchanged. Full logs
and comparisons are in /private/tmp/abcd-round74. Increment II is not started.


## Round 74 increment II: closed class publication

The common ClassMethodSites index now retains typed Sendable member/field
literals once per unique original entity. Existing immutable creator input owns
the original decoded operations; layout/finishing never re-decodes a creator per
class. Distinct field key entities are sorted/deduplicated before string reads.
The actual allocator charges shared control blocks before allocation. The
file/index owner retains this charge while class layouts borrow immutable
literals; class output does not obtain another unbudgeted buffer.

Sendable kind survives the normal class context, layout, preparation, value
operand verification and original creation/commit/exception mapping. A typed
constructor directive binds the exact method and creation pool identity. The
closed proof checks external capture targets, every member family's module
uses, heritage, omissions, initializer/definition gaps, canonical literal and
primitive type spellings; the finished and tree checks repeat it independently.
No capture guard, creation event, constructor assignment or exception edge is
removed. Resource/work failures retain their classes.

Primitive masks are emitted as storage-only TS descriptions with no initializer,
and the constructor directive is first. NONE/TYPE_REF/GENERIC remain a located
kTypeSpelling; shared module/read support remains separately gated. Newly
published Sendable output alone becomes .ets/arkts; ordinary and concurrent
records retain their existing syntax and extensions.

Acceptance uses actual engine output recompilation with DevEco's dynamic TS
parser, exact record identity and no --use-define-semantic. The independent probe
compares kind, constructor length/flags, heritage, member literal order, field
mask/order/split, ordered [[Set]], environments and module exports/requests. Node
covers ordinary calls and discriminating setter traces only; shared heap/type
checks are not verified there. See the fixture README for compiler identity,
options, hashes, scale/budget and mutation coverage.

Static storage requiring the frontend's static-initializer member remains a
typed preparation gate in II; no initializer is synthesized or silently dropped.
Class-field grammar also rejects constructor/static-prototype keys rather than
using MemberExpression IdentifierName rules or crashing the pinned frontend.

Malformed field descriptions are retained as typed per-literal failures in the
shared index, so an invalid mask/split/key refuses the owning class instead of
poisoning unrelated records. Resource/work exhaustion remains an index-owner
failure. The class refusal retains the field ordinal, known raw mask and leaf
field-decoder cause.


## Round 74 increments III/IV: imported types and shared initialization

Baseline 646fb819. The shared access proof is separate from ordinary synchronous
module admission. `ModuleDomain` survives raw semantics, Value IR, SymbolExpr,
creation context, guard verification and recovery. Sendable loads may throw and
commit acc only on success; a named hole check remains an ordered source read.
No external-initialization premise is synthesized. Domain and slot/binding/request
identity are rechecked at the original/working guard pair and final enclosure.
Member-root environments and ordinary nested closure environments remain distinct.
Static initializer and inherited nested-arrow module domains remain gated.

`sendable_types.cc` indexes import aliases, expression paths and field names once.
It associates an exact GENERIC field with its original instance [[Set]] and the
import-backed constructor path. This selects a recompilation spelling, not a
unique source type. No initializer, import or effect is added. Preparation and
independent verification derive the same witnesses from the finished constructor;
printing only renders witnesses already checked with the full ClassTree. Scratch
and retained witnesses are charged before reserve; sorting is O(F log F), with
string-comparison work charged by code units; expression indexing is O(E).
Printer path work is linear in the emitted type paths. Budgets remain unchanged.

`SharedModuleProof` binds the real shared attribute/table to its record and file;
CJS, TLA, JSON and unsupported lazy requests fail explicitly. Entry recovery still
proves all effects and exports. No shared partial record is published. Import
printing rechecks the proof and includes the directive in its byte count.

For o14/24892, engine output recompiled with DevEco es2abc, `--module --merge-abc
--extension=ts --opt-level=0 --record-name=&entry/src/main/ets/utils/LruCache&`,
retains shared=true, non-CJS/non-TLA, request/import/export identities, Sendable
creation, constructor length 0, lock_/32 and books_/32 with instanceCount2, three
async members and their ordinary callback relations, no extra sendable env, two
guarded Sendable external slot-0 reads, constructor [[Set]] and four put calls in
order. The richer test probe compares these facts and async protocol counts; it
does not replace the engine's independent SSA/control/exception recovery proof.
See the fixture README for compiler identity and oracle limitations.


Acceptance for III/IV (workspace WIP, no commit): full build passed; fixed65/65;
new tests3/3; targeted105/113 (the eight registered class/captured-receiver/async
failures); CLI/corpus/failure/receipt/manifest sweep71/72 (registered family source
failure only). Stale V3 shared-policy and LruCache refusal expectations were
updated to exact new completion plus retained old-contract rejection. Both
exploration comparisons against explore74b/explore74bs report old521/620 and
source567/620, only LruCache changed, zero status regressions and zero existing
text/extension/position drift. Logs, comparison JSON and target round-trip JSON
are under `/private/tmp/abcd-round74iii/`. No held-out/full-corpus/RSS benchmark
or full-suite run was performed.
