# Materialized selection consumers

These are independent source oracles. Production only reads the ABC files.
Generated using the installed DevEco SDK es2abc (ABC 24.0.0.0):

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name=consumers --source-file=consumers.js --output tests/integration/fixtures/selection_consumers/consumers.abc tests/integration/fixtures/selection_consumers/consumers.js
"$compiler" --module --merge-abc --opt-level=0 --record-name=member --source-file=member.js --output tests/integration/fixtures/selection_consumers/member.abc tests/integration/fixtures/selection_consumers/member.js
```

SHA-256:

- consumers.abc: `42c57dd2c46c6714d005157547a2fae70d4ab3d372773d3f55a52a2985d9aca1`
- member.abc: `dc2d4f895c9364dbde5a64d5946aaeeeb0a1bb7aad39bdba54eefceb795f6c08`

`consumers.js` covers `!!x && f(x.p)`, a retained `!q && p` result,
comparison chains, a three-term boolean call argument, and a default selection
that reads its property twice on the truthy path and uses the selected result
in a call, a property assignment and a return. The generated multi-term case
keeps the intermediate phi assignments; native logical syntax is not required.
`booleanJoin` remains a positive regression: it recovers once the deferred
step-4 eligibility is withdrawn. It must not require TreeFlow weakening.

The CLI test inspects retained method 520 (property reads at PC43/72 plus
the later log-member read at PC84) and multi method 464 (six ISTRUE and two
ISFALSE instructions with real branches), then compares 21 modes under both flags (42 executions). Modes
include getters/call/log/setter throws, false operands at each position,
rebinding between terms and objects whose valueOf throws. ToBoolean must not
invoke valueOf. Four mutants per flag alter property identity, default value,
short-circuit connective and getter evaluation count; each must be detected.
The oracle compares exception constructors and traces, not messages or stacks.
No finally-throw case is included: those require an ABC-handler-derived oracle.

`member.js` retains the separate member-reference gap. Both flags must report
CallReferenceError::kNotInline, rather than the removed missing-consumer error.
This refusal is not counted as a differential completion.
