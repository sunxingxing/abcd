# Anonymous Function Expressions

## Source Shape

Empty runtime names are valid for ordinary and async function expressions.
They are not identifier bindings. `WriteCallable` now admits this case without
relaxing name validation for constructors, methods, or nonempty identifiers.

The source writer creates a fresh private scope and returns a computed empty-key
data property containing the ordinary or async function expression:

```js
(() => {
  const $function = ({[""]: function() {
    "use strict";
    // The generated ABI function register is initialized from $function.
  }})[""];
  return $function;
})()
```

The computed key applies SetFunctionName with the empty string without calling
a mutable intrinsic. The function itself is not an arrow: ordinary `this`,
`new.target`, and constructibility are retained. Its immutable private binding
initializes the independently writable ABI function register. Parameter, global,
and capture naming continue to reserve the generated `$` namespace.

Rest or neutral-default parameters inherit strictness from the outer scope;
their bodies do not receive an illegal strict directive. Existing parameter
count and length proofs, async normalization, typed capture rebasing, and body
admission remain mandatory. The wrapper adds constant source size and work.

## Evidence and Limits

Both production CLIs compile. Read-only review found no concrete defect.
ABC-only debugger inspection of pasteboard PageA entry 7283 now takes child
7061 past its former `FunctionEmitError::kName` refusal into the shared semantic
template pipeline. That child still fails `SemanticProofError::kInternalValue`
at original PC53 in its actual owner context. No additional complete record
or executable-source equivalence is claimed.

Tests, generated-JS execution, and RSS measurement remain paused by user request.
Deferred checks include empty `.name`, self identity after ABI-register writes,
ordinary calls and construction, async calls, rest/default `.length`, strictness,
nested anonymous functions and captures, and output-budget boundaries.
