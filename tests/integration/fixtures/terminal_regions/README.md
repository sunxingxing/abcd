# Terminal regions

Independent JavaScript source oracle and real DevEco es2abc output. The input
contains an early return after internal conditional effects, the same body under
a native try/catch, and an independent double-terminal if/else. No application
name, platform emulation or paired source is an engine input.

Compiler:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`

From the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=terminal --source-file=terminal.js \
  --output tests/integration/fixtures/terminal_regions/terminal.abc \
  tests/integration/fixtures/terminal_regions/terminal.js
```

Record 319; methods 419 (`early`), 475 (`independent`), 503 (`protectedEarly`).
`abc_semantic terminal.abc 503 --instructions` confirms outer and inner branches
at PCs 22 and 46, an internal conditional at 53 with arm join at 91, early return
at 115, normal tail return at 157, and catch return at 199. Thus the early arm has
real internal effects and a join, not a renamed straight-line return fixture.

`terminal_regions_cli_test` exports with both flag sets and executes the emitted
module and this independent source in Node. It compares 360 scenarios per module:
three functions, all eight boolean triples, and fifteen throw configurations
(no throw, or getter/call throw at each of seven hooks). Assertions include exact
log order, receiver identity, return value, exception constructor and caller-finally
trace. No error message or stack equality is claimed; this is source-oracle
validation, not execution on the Ark VM. Mutants erase the early return, duplicate
an effect, exchange conditional effects, and corrupt the catch argument. Every
mutation must change the observation. IR tests independently require virtual exit
for the double-terminal case, and reject forged graph/ownership/completion facts.

SHA-256:

| File | Hash |
|---|---|
| terminal.js | f2078e1a7953055d07e806cdb97acd3145cae51d6592c7a8284c2f9c5eebf344 |
| terminal.abc | 005bd5ef870a2c2e6d998bd9bec6c0a3bf6d7df8599a83a85268addcdc3a4bb4 |
| es2abc | 4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca |
