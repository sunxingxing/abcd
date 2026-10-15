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

Production gates remain: semantic lifting refuses Sendable source; the source
class-site index remains ordinary-only; shared entry remains kModule/kShared;
both V3 policy predicates remain disabled. The new Value kind is groundwork,
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
