# Private declaration prefix before native rest

The engine consumes ABC only. `rest.js` is a separate ordinary-source oracle:
`find` creates a local helper before COPYRESTARGS; `captured` captures the
ordinary object parameter; `recursive` captures its helper's own cell and the
object parameter. The prefix's order, environment/cell identity and resource
exception sites remain in the original IR. Only the rest-array binding moves
to the native signature; .length remains 1 and each invocation gets a fresh
array. Node tests cover argument getters/throws, helper-body getters/throws,
missing/undefined/excess arguments and per-call isolation.

`captured_rest.js` also includes the exact requested
`function find(obj, ...args) { function visit() { return args; } return visit(obj); }`.
It retains the existing typed `kArgumentUse` refusal: the compiler first stores
the physical last-argument scalar into a captured cell and later overwrites it
with the rest array. Proving that first value dead needs its own cell-value
correspondence. This slice does not infer it from a private declaration or
remove the argument-use check. The test asserts this refusal and its location;
this fixture is not counted as a successful source differential.

Compiler identity, SHA-256, options and hashes are in `manifest.json`. From
this directory:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output rest.abc rest.js
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output captured_rest.abc captured_rest.js
```

The separate round-trip CTest is registered only when that compiler exists.
It hashes the executable, recompiles engine output with the same options and
checks all three functions: COPYRESTARGS index 1 and two formal slots. Quoted
identifier export aliases are adapted to identifier aliases for this compiler;
no function body/signature or import is changed. Node independently checks
.length === 1. The checked-in ABC/Node differential always runs when Node is
available, without requiring an installed compiler.
