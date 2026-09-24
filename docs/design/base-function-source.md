# Base Function Source

## Scope and Evidence

Emit an executable named strict function expression for an ordinary DEFINEFUNC
target with BASE_CONSTRUCTOR attributes, no sendable flag, and a supplied
validated frame. The caller supplies the runtime-visible parsed function name,
not the raw ABC method name, and the matching creation/body/frame from the
pinned 7.0 runtime semantics. This is not record publication or historical
semantic-profile selection.

The pinned `ObjectFactory::NewJSFunction` selects the with-prototype function
class for BASE_CONSTRUCTOR. `RuntimeDefinefunc` installs length from the creation
instruction, not the declared parameter count. `FrameLayout` specifies locals,
function, new.target, this, then declared parameters. These distinctions must
survive source generation.

Use native function syntax, not an interpreter or callable proxy. Restrict the
first implementation to length equal to declared parameters and strict-safe
ASCII names outside the reserved generated `$` namespace. Refuse other kinds,
DEFINEMETHOD, sendable, invalid frames, names and length mismatches. Default/rest
parameter recovery and anonymous/arbitrary names remain separate work.

Current signature lowering also handles a creation length smaller than the
ABI parameter count when an exact rest prologue is absent and the body has no
`COPYRESTARGS`. It puts the side-effect-free `= void 0` marker on the first
formal at the creation length. This changes native JS `.length` without
changing the value passed to the bytecode body; the original body remains
responsible for any real default expression, including its control flow and
effects. Exact rest prologues also admit dynamic 12.0.2.0. The neutral-default
marker remains limited to dynamic 12.0.6.0 and 13.0.1.0; getters and
setters still reject length mismatches. A non-simple formal cannot contain a
body-level strict directive, so ordinary function expressions inherit strict
mode from an outer IIFE, arrows from their enclosing IIFE, and class members
from class syntax. This is semantic signature recovery, not reconstruction of
the original textual default initializer.

## Lowering

Generate private `$pN` parameters, mutable `$rN` register bindings, and `$acc`.
When an unchanged method has debug scopes, source parameter names may replace
`$pN` only after a typed entry-MOV proof maps each ABI parameter to one unique
local whose debug range starts at the end of that prefix. Overwritten ABI slots,
duplicate names, branches into the prefix, invalid/strict-reserved identifiers
and binding collisions fall back to `$pN`; no body operation is erased. The
same proof serves created functions, constructors and ordinary class methods.
Global name loads additionally check a sorted source-formal index, so a newly
named parameter cannot shadow a recovered free identifier. Debug type strings
are not trusted as source types; rest/default parameters remain unresolved.
Locals start undefined; hidden slots copy the named expression's immutable self
binding, native new.target and strict this; parameter slots copy parameters.
Never alias a mutable register to the immutable function binding. Missing actual
arguments become undefined naturally. Extra actual arguments are not inspected
by the supported body subset.

Use WriteLocalLexicalBody for the actual instructions. Its value checks exclude
internal values from language operations and reject capture, home-object uses,
nested creations, exceptions, resumption and non-linear flow. This avoids
pretending that a wrapper repairs an unsupported method body. Every expression
evaluation creates a distinct function and prototype. Source text, debugger
metadata, stack formatting and allocation-failure timing are not reproduced.

Preflight the complete wrapper and body into a discard stream before output.
No body-sized string buffer. Both passes prepare their own bounded body plan.
An actual allocator failure on the second preparation can leave a prefix, as
can IO failure or changed backing constants. The deterministic configured-budget
checks run before output, but this is not a transactional stream API. Future
record publication must use temporary files and atomic rename.
Register-name storage is fixed-width arrays plus views, checked before allocation
and subtracted from the shared analysis budget. Inputs, transient strings and
caller output are separately admitted by the caller. This primitive does not
prove the future concurrent process-wide 1 GB bound.

## Plan and Validation

1. Add a failing source test for a strict named constructor with all hidden slots
   and a parameter. Add typed refusal tests and untouched-output assertions.
2. Implement source/base_function.h and .cc; wire CMake and retain typed body
   failures. Validate frame canonicality using BuildFrameLayout.
3. Execute generated functions in Node: call/construct, primitive and undefined
   this, new.target, self identity, missing arguments, local initialization,
   mutable register independence, names, length and prototype descriptors.
4. Test exact/short output and scratch budgets, stream errors, unsupported bodies,
   200/201 and geometric register counts. Run Debug and sanitizer tests, isolated
   1 GB address-space/RSS check, independent review, and commit.

Wrapper overhead is O(R + output bytes), O(R) scratch for R registers. Body
analysis retains its documented sparse lexical complexity; no frame-by-CFG
matrix, full-frame copy per instruction, or output-sized buffer is added.
