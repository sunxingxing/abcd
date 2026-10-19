# Retained Boolean selection inside a native for-of body

`main.js` is an independent Node oracle. Only `main.abc` is supplied to the
engine. `scan` (m395) contains `left(value) || !right(value)` inside a for-of,
plus a transparent RHS handler and effectful close handlers. The front end
materializes the negation through true/false arms. The original selected phi
must bind through PhaseControlUnit to the compressed control graph. `outside`
contains the same decision outside a loop.

Compiler: DevEco dynamic es2abc, SHA256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 \
  --output=tests/integration/fixtures/for_of_selection/main.abc \
  tests/integration/fixtures/for_of_selection/main.js
```

The unit test drives production evaluation, draft, TreeFlow and recovery, checks
wrong phase/join mapping and dropped/duplicated/reversed selection consumption,
plus existing iterator/close mutations. Register shifts, inserted blocks and
200/201/400/800 graph sizes exercise the same proof with exact byte/work limits.
The ordinary phased-prefix test compares both binding interfaces outside a
protocol projection, including 200/201/400/800 retained selections.

The Node test executes engine output: empty and nonempty input, short-circuit
call counts, a truthy object whose valueOf must not run, getter/call/body throws,
iterator closing, and the same expression outside the loop. V2 acquisition and
V4 external-exception premises apply. Raw Hole/native boundaries and Ark-specific
iterator behavior cannot be established by the Node oracle. No finally-throw
source oracle is used.

SHA256:

- `main.js`: `f35b7941b6b1ac4aac101903ab977def34b386ffdecb6ea8abae9bd209e86f8f`
- `main.abc`: `b137e8bbc3536e21fb2f03e456967c52ce4b6bc41acf7f2c82ccde93692c26a1`
