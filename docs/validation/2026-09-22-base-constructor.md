# Base Constructor Source Validation

Scope: native constructor method emission, sharing canonical ABI register
initialization, simple formals and bounded body emission with base functions.
This is not yet invoked by the record exporter and does not complete instance
fields, globals, full signatures or record source integration.

- Debug build completed; full CTest: 98/98 passed, 28.26 seconds.
- ASan/UBSan: base_constructor_test, base_function_test,
  created_function_test and local_lexical_body_test: 4/4, 31.12 seconds.
- Node executes generated constructors: new-only invocation, length/name,
  prototype descriptor, primitive/object return rules, thrown-value identity,
  function/this/new.target ABI slots and alternate new.target.
- Property assignments preserve inherited setters, write order and exceptions;
  they are not converted into field declarations.
- Rejections cover sendable attributes, invalid binding/frame, mismatched
  length, unsupported sentinel/capture bodies and zero-output budget failure.
- Tests cover 256 formals, exact/short output and scratch budgets.
- Read-only review found no blocking issue; its required caller proof is
  recorded in the public header and design document.

The first full run exposed an existing debug_info_test fixture race: unlink
before lazy reads changes the input identity. Repeated execution reproduced
the failure. Cleanup now follows the read; 100 consecutive runs passed.
Production identity checks are unchanged. The subsequent full run passed.

No real-corpus constructor-source recovery or whole-process memory ceiling is
claimed by these synthetic execution tests. No corpus classification changed.
Actual class-site target, no-extends heritage and stable private class binding
proofs are required before production record integration. Derived constructors
and unresolved captures remain outside this emitter's contract.
