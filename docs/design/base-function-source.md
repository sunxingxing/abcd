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

## Lowering

Generate private `$pN` parameters, mutable `$rN` register bindings, and `$acc`.
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
