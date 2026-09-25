# Import Syntax Recovery

The shared module writer classifies decoded wire imported names into named,
default and namespace forms using an exhaustive typed enum. The reserved
wire labels `default` and `*` are recognized only at this format boundary.
Default imports now use `import $importN from ...`, which retains the same
live binding as `import { "default" as $importN } from ...`.

Named imports and indirect exports print ASCII IdentifierName keys without
quotes. ModuleExportName permits keywords, unlike local binding identifiers.
Non-ASCII, empty and punctuation names retain exact string-literal escaping.
Default reexports remain inside braces; namespace reexports retain their
separate form. Local import aliases, module specifiers, slot indices and
indirect export resolution do not change.

Request-only declarations remain in ABC request order before binding
declarations. Import-slot order is not necessarily request order (PasteBoard
is an example); deleting those declarations without a request-order-aware
emission plan would change the order of module dependency traversal. This
change intentionally does not perform that deletion or rename local bindings.
Original local-name recovery still requires shared scope/collision proofs.

Names are decoded once per existing writer visit. Classification and output
are linear in name length with constant extra scratch, using the existing
output budget and buffered atomic semantic-failure boundary. There is no new
dependency, name cache, sorting pass or semantic string dispatch.

## Evidence

Both production CLIs build; read-only review found no concrete defect.
ABC-only export to `build/pasteboard-import-syntax-01` retains eight incomplete
application records and the two host-constructor refusals at PC14. PasteBoard
prints `import { CommonConstants as $import0 } ...` and default imports for
hilog, image and pasteboard, retaining the preceding request-order declarations.

Tests, generated JS execution and RSS measurements remain paused. Deferred
checks cover default live bindings, namespace imports, default and namespace
reexports, keyword/empty/Unicode keys, dependency evaluation order and output
exhaustion. Export evidence does not establish runtime equivalence, original
local names or completion of the overall source recovery goal.
