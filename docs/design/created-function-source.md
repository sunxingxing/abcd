# Created Function Source

Connect a decoded creation instruction and caller method identity to the actual
target's executable source expression. Reuse LiftFunctionCreation,
ResolveMethodTarget, ReadCallType, BuildFrameLayout, DecodeMethodBody and
WriteBaseFunction in that order. Keep typed failures; never default failed
metadata reads to a missing annotation. MethodLookup is shared per file.

Pinned MethodLiteral::ParseFunctionNameView strips the prefix through the final
`#` only if the raw name starts with `#`, then strips the final `^` suffix. A
non-decorated name is unchanged. JSPandaFile::GetMethodName constructs its byte
view using UTF-16 length, so the initial adapter accepts only nonempty ASCII
raw names with matching byte/UTF-16 lengths. Do not guess Unicode equivalence.
The final strict-safe identifier restrictions remain WriteBaseFunction's job.
Names are necessary textual metadata, not semantic tags.

Caller supplies a decoded instruction from the unchanged caller and a matching
file/header/index/lookup. This adapter binds the callee, not the caller control
flow, capture environment, record, module or dynamic closure instance. The body
writer rejects all uses requiring captures or home objects. Do not recursively
walk targets or merge runtime objects by target ID. Each emitted expression
still creates a fresh function when evaluated.

Decode budget and retained decoded-body capacities share the source scratch
budget: decoding finishes before source analysis and its live body storage is
subtracted before emission. Metadata index, lookup and constant-string allowance
are caller-admitted. Resolution costs O(log regions + log methods); name parsing
is O(name bytes) with no copy. Remaining bounds are those of the reused passes.
This remains a synchronous primitive, not the required final task service.

Implementation/validation sequence: add a failing binary-backed target fixture;
implement the adapter; test actual pool resolution, frame annotation defaults,
code-prefix validation, decorated names, invalid targets/names/lengths/budgets;
execute its emitted function in Node; run full regression and sanitizer tests;
review and commit. No application record output or corpus coverage is claimed.
