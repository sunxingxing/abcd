# Shared optional-expression / switch-break control

The engine input is the real es2abc `switch_optional.abc`; JavaScript is the
independent Node oracle. The old engine at ca4fc005 refuses duplicate ownership;
the new forwarding view retains the original inert jump once and verifies its
other incoming epsilon ports. This fixture does not exercise effectful native
switch fall-through (still a typed refusal).

Compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=switch_optional \
  --source-file=switch_optional.js \
  --output tests/integration/fixtures/switch_optional/switch_optional.abc \
  tests/integration/fixtures/switch_optional/switch_optional.js
```

| File | SHA-256 |
|---|---|
| switch_optional.js | 8301ff08a5112b80705d56499d14c351ffa4fa9b88316c60518e07c561939d43 |
| switch_optional.abc | ee0ab0474511bbc39de44cd923ce0947ce489624416eb24154bece66474413c6 |

`switch_optional_cli_test`: 800 independent original/emitted observations per
contract, covering both functions, every case/miss, null/undefined/changing
receiver getters, zero/multiple loop iterations, getters and calls that throw,
and exact receiver/order. Four changed-source mutants must be observed.
`switch_optional_record_cli_test` separately compares the actual o19 callback
against the original .ets source in 960 scenarios, with enum/argument getters,
callback/log effects and exceptions; source is never engine input.

Default and O0 `grouping_bytecode.py` runs compile the original and emitted
fixture. Module entry is exact. The functions differ by switch-to-if layout,
strict-equality orientation, feedback slots, register/copy allocation, and a
redundant numeric conversion in the original counted increment. No handlers
exist. Strict report: 0 exact / 1 differing record per mode, not bytecode-equality
passes. The full differences and semantic boundaries are recorded in
`docs/design/source-recovery/switch-case-ownership.md`. No checker allowance or
register/branch normalization is used; no Ark runtime execution is claimed.
