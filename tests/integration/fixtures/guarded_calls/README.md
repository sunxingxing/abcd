# Checked capture call arguments

Independent JS source oracle and real DevEco es2abc fixture, ABC 24.0.0.0.
The engine consumes only `calls.abc`. Paired `calls.js` is used by the Node oracle.

Compiler: `/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`.
From the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=calls --source-file=calls.js \
  --output tests/integration/fixtures/guarded_calls/calls.abc \
  tests/integration/fixtures/guarded_calls/calls.js
```

Verified with `abc_semantic calls.abc 420 --instructions`: captured `ldlexvar`
PC 57, named `throw.undefinedifholewithname` PC 59, `callthis3` PC 85.
Both the guard and member call dispatch to handler PC 128. The method has a
user catch and finally; catch reads the local `saved` value loaded before try.

`guarded_calls_cli_test` exports with both contracts and checks the engine's
native member-call syntax. Fourteen Node differential runs cover early TDZ and
initialized reads, callee getter, earlier/later arguments, call throws, callee
replacement and receiver replacement. Five mutations per contract exercise
receiver detachment, repeated getter, removed checked read, argument reordering,
and premature declaration. The IR test additionally rejects duplicated guard
membership, an incorrect success Commit, changed exception mapping, fake
composition roots, multiple consumers and unsupported catch staging.

The exception oracle compares constructors and control/value/log traces, never
message or stack spelling. es2abc lowers native lexical TDZ to the named hole
check; this test does not claim to execute ABC under Node or to compare Ark and
Node error messages.

SHA-256:

```
673e5a4dc94067d1da9ffa31b62d8736d7a9d8180d686c2fc5f3042ebcb508bf  calls.abc
```
