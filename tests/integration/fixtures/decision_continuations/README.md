# Decision continuation boundaries

Independent source-language oracle and real DevEco es2abc output. No application
names, source text or mock platform are engine inputs. The engine receives only
the ABC files. This fixture tests the Round 40 continuation proof, not a new
source-language contract.

Compiler:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`

From the repository root (substitute `finally` for `decision` for the second file):

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=decision --source-file=decision.js \
  --output tests/integration/fixtures/decision_continuations/decision.abc \
  tests/integration/fixtures/decision_continuations/decision.js
```

`decision.abc`: record 323, methods 423 (`run`), 451 (`asyncRun`), 507
(`protectedRun`). `abc_semantic … 423 --instructions` shows the outer branch at
PC 17 targeting 145; both conditions at 40 and 62 target the shared body at 128.
The private arm has an internal branch at 69, joins at 107, and returns at 127;
the shared body reaches 145. This is a genuine shared decision with a private
terminal arm, not a straight-line return fixture. The async version has its
outer tail at 150 and both short-circuit branches (43/65) target 133, with a
private return at 132. The protected version has a real handler and branches
41/63 targeting 133, a private return at 132 and outer tail at 150.

`finally.abc`: record 283, method 410 (`finallyRun`). This independent compiled
wrapper calls `hooks.body` in a try and calls `hooks.finally` on both completion
paths. During differential execution, `body` invokes one of the three decision
functions. Thus early return, synchronous throw and async results are tested
through actual recovered finally code, in addition to the runner's caller-finally.
It does not claim support for every finally lowering or for awaiting inside finally.

`decision_continuations_cli_test` exports both files under both flag sets and
compares 2,016 runs per flag set: three functions, wrapped/unwrapped, all four
boolean inputs, and no throw or getter/call throw at each of ten hooks. It compares
exact log order, receiver identity, return value and exception constructor; it
does not compare message or stack and does not claim execution on the Ark VM.
Seven mutations must change observations: erase the private return, duplicate the
shared effect, eagerly evaluate the second condition, erase the outer tail,
exchange internal conditional effects, corrupt the catch argument, and erase
finally's effect. Mutations must actually apply to generated source.

SHA-256:

| File | Hash |
|---|---|
| decision.js | 55aaa5e5f3af16da5c4a0494298d5457e7f263eb9537fd1549440cf56d392f66 |
| decision.abc | 524873e1e00d0c4ac7b22bae5b113393a230073eddee3f22a0b5c196242b085f |
| finally.js | 659e2d60a0e39fb322dc77927aa15bee29e7bfeaa6e994d20c4f34f3a4755930 |
| finally.abc | f9cd2d2bdb9dfbc1d2a6ef5b30a0f0b1dbce66b54fc44c0defd36ec745ad83e3 |
| es2abc | 4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca |
