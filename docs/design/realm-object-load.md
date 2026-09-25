# Realm Object Load

LDGLOBAL now lifts through the existing constant-object format validator into
GlobalObjectLoad, a distinct semantic operation. The pinned interpreter loads
the VM's global object directly into acc, with no user code, lookup or throw.
The typed value rule yields Defined; the AST retains the same typed identity.
This adds constant per-instruction work and no new retained tables.

The printer refuses this expression with SourceError::kRealmBinding until an
owner-validated realm-object source binding is available. A mutable globalThis
property/name, Function constructor, or lexical free identifier is not assumed
equivalent. No new execution premise has been installed. The user has been
asked whether a versioned lifetime globalThis identity/non-shadowing premise
is acceptable; pending that decision, no source substitution is performed.

ABC-only inspection of pasteboard callback 7061 now builds 27 IR instructions,
3 blocks and 36 SSA values, rather than failing at LDGLOBAL PC6. Detached
function-source inspection proceeds to an independent local-module value
refusal at PC46. In actual PageA entry 7283 emission, deeper debugger tracing
reveals target 7061 first fails FunctionEmitError::kName, before shared body
printing: its ordinary function has an empty runtime name. Therefore the
realm boundary is a known later requirement, not the sole current refusal.
Next work must preserve anonymous function identity/name semantics as well
as resolve the explicit realm-binding contract.

Both production CLIs build and read-only review found no concrete defect.
No new complete or incomplete record output is claimed. Tests, generated-JS
execution and RSS remain paused. Deferred cases include malformed LDGLOBAL
operands, ordinary and phased SSA effects, globalThis rebinding/shadowing,
cross-realm bindings, nested-function propagation and typed refusal without a
binding contract. The source failure enum is extended without renumbering its
existing values.
