# ISA 12: V10 recovery checkpoints

The baseline is 602a4a1e. Four predicates are independently versioned in V10;
V9 is not reinterpreted. The work proceeds computed names, dynamic imports,
namespace imports, then array patterns. The accepted differences and pinned
citations are in [language-source.md](language-source.md).

## Computed anonymous own definitions

`ValueComputedOwnWrite` keeps its `PropertyNameSetting::kPropertyKey` and
OwnWriteKind. LiteralPlan now records the key and stored-value ownership edges,
not a later member assignment. `VerifyLiteralPlan` freshly checks the actual
base allocation, key occurrence, stored SSA value, unique anonymous creation,
complete forest, canonical use census, increasing commits, no escape and catch
snapshots. Function anonymity/self binding is independently checked with the
file-backed child tree in FamilyEnclosure; it is not guessed from the name of
an opcode. Unsupported class-definition ownership remains a typed refusal.

A computed key is evaluated once before the fresh function expression. Ark's
key coercion belongs to the store; native syntax coerces it before anonymous
NamedEvaluation. Only unobservable fresh function creation intervenes here:
no callback, field initializer or escaped function may occur in that gap.
Allocation/resource failures are already excluded. This does not move a key
coercion across an observable expression. The numeric function-name difference
alone uses ComputedFunctionNames. Staged source assignments, existing/nonfresh
functions, forged [[Set]], wrong key uses and observed partial literals refuse.

For this previously unsupported case the native forest interval also checks
the common EffectOrder exact must-reach successor/predecessor relation for each
ordered event. Thus pure CFG splits and register aliases do not require a
same-block admission rule; early exits/re-entry/different effect histories fail.
The single scan reuses the existing effect index; no per-property CFG walk or
new graph-sized table is added. Old successful literal plans keep their path.

DataProperty has an optional owned computed-key child. TreeStructure consumes
key then value once; TreeOperands rechecks both actual uses and the name-setting
attribute. OriginalFlow retains the own-definition property port. RecoveryFlow
rebuilds key -> value -> define, including exception exits. Every recovery
projection sees the same children through VisitExpressionChildren. Printer
preflight and both sinks use the computed NamedEvaluation context, retaining
anonymous function/arrow syntax and the original position spans.

SDK default/O0 fixtures cover both widths, string/number/-0/symbol/object keys,
throwing key coercion, sibling getters, descriptor flags, inherited setters and
arrow names. The accepted numeric-name difference is explicitly excluded; no
Ark runtime execution is claimed. C++ mutations and exact admission test
200/201/400/800 definitions, including copies and one-op-per-block flow. SDK
scale covers 200/201/400/800 key lengths. A preliminary 800-function single
literal also reached the pre-existing printer input-storage limit (93,372,408
requested vs 72,241,472 remaining); that is retained as a resource observation,
not counted as a complete SDK fixture or bypassed by a budget increase.

Strict original-ABC round trips: 2 exact / 2 differ / 0 unverified. The two
basic-fixture differences are export/declaration ordering and anonymous method
ordinals: make and arrow retain their distinct 17/23 bodies at their respective
creation sites. The wide fixture is byte-identical at default and O0. No register,
branch, handler or metadata normalization converts a difference into a pass.

## Dynamic import

`DynamicImport` / `ValueDynamicImport` are appended typed operations with one
specifier input and a successful promise result. The production register adapter
reads/writes acc; malformed operand forms refuse before SSA. RuntimeDynamicImport
(`runtime_stubs-inl.h:1839–1884`) retains the original specifier and current
function's file/record, creates a fresh promise and queues a job. Conversion and
loading failures reject that promise (`builtins_promise_job.cpp:206–343`); only
allocation can fail before the operation's Commit. The normal effect bound keeps
queued work/host observations ordered, but before-throw effects are allocation,
not an invented synchronous ToString throw.

`DynamicImportExpr` owns the original specifier occurrence. The existing
independent TreeOperands and OriginalFlow/RecoveryFlow intrinsic-action mapping
check the input, one Before/Commit and caller exception exit on every projection.
Its syntax is a call expression for grouping and a lexical-record builtin, not a
mutable global function or a fabricated callee. Both DynamicImports and
ModuleNamespaces are required: the job can also expose native/CJS exports; the
approved namespace difference is not silently asserted to be a standard wrapper.
The current function's record identity is closed by the ordinary file/family
publication context. V9/previous flags refuse at the located
`EvaluationInputCheck::kDynamicImportPremise`; independent tree verification also
refuses a forged older contract.

Two default/O0 SDK ABCs complete. Node compares values, eager operand getter
errors, post-import errors, module rejection, promise identity and ESM namespace
identity. Object-specifier coercion timing and native/CJS namespace observations
are the two explicit approved exclusions, never Ark execution passes. C++ tests
cover wrong source operands, replacement of the import, older-contract forgery,
malformed raw operands and exact byte/work boundaries for 200/201/400/800 imports
with normal and split CFGs. At 800, the EvaluationPlan boundary is 173,016 bytes;
work is 68,088 normal / 69,687 split. SDK array fixtures at those counts compare
all retained promises and coercion counts. Strict round trips are 2 exact / 0
differ / 0 unverified. Evaluation originals are not engine inputs.
