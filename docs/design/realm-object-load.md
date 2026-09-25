# Realm Object Load

LDGLOBAL now lifts through the existing constant-object format validator into
GlobalObjectLoad, a distinct semantic operation. The pinned interpreter loads
the VM's global object directly into acc, with no user code, lookup or throw.
The typed value rule yields Defined; the AST retains the same typed identity.
This adds constant per-instruction work and no new retained tables.

The printer refuses this expression with SourceError::kRealmBinding until an
owner-validated realm-object source binding is available. A mutable globalThis
property/name, Function constructor, or lexical free identifier is not assumed
equivalent. On 2026-09-25 the user explicitly rejected a lifetime globalThis
identity/non-shadowing assumption and required a trusted host-provided global
object binding instead. No name-based substitution or such execution premise
was installed at that decision. The user's later explicit approval supersedes
the globalThis refusal; see `source-contract-v2.md` for the approved scope and
its rollout status. Until the new policy is activated or an explicit trusted
binding is supplied, source emission must retain `kRealmBinding` refusal.
An API for supplying that binding is now available as `HostRealmBinding`;
a free identifier alone cannot establish its identity
or protection from shadowing. This decision does not settle normal-call return
value admission or expand the approved pending-exception contract.

## Explicit Host Binding

An embedding host can supply `HostRealmBinding` through `BuildRecordSource` or
the lower-level `GlobalScope`. Its immutable lexical binding must be initialized
to the original realm object and remain visible for the output's lifetime.
The borrowed name is stable during synchronous emission; artifacts own a copy.
This is a trust boundary, not a proof inferred from the identifier text.

The shared printer checks strict-safe ASCII spelling, enclosing/module bindings,
class/self names, the printer's own function/class name, parameters, rest
bindings, accumulator and register names.
Ordinary global accesses colliding with the injected name refuse too: an
injected lexical binding must not silently replace a VM global-property read.
Only a successfully admitted binding can render the typed GlobalObjectLoad.
No `eval`, Function constructor, reflection or fallback globalThis lookup is
introduced. Existing child-function and class scope propagation carries the
same capability and repeats local collision checks.

PrintedSource and PrintedTemplate own a `HostRealmRequirement`, even when only
a descendant needs the object. Capture binding preserves it; JSON/export
metadata names the required binding. Owned name capacity is charged before
copying. Checking costs are linear in the supplied name/name-table bytes,
with the existing sorted-scope binary lookups; storage is one owned name per
artifact. Future source cache identities must include binding configuration
and trust-policy revision, not the name alone.

No production CLI currently supplies this capability. Default refusal remains
until V2 integration. Tests, JS execution and RSS measurement remain paused;
new binding acceptance/collision and nested propagation regressions are deferred.

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
