# Shared abrupt completion boundaries

Real dynamic es2abc input; JavaScript is only an independent Node oracle. The
engine receives `shared.abc`. `classify` has nested logical guards and an early
return shared by two predicates; `dispatch` has guarded dispatch cases and a
common continuation; `protectedDispatch` contains the dispatch inside try/catch.

Compiler:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=shared --source-file=shared.js \
  --output tests/integration/fixtures/shared_terminal/shared.abc \
  tests/integration/fixtures/shared_terminal/shared.js
```

| Input | SHA-256 |
|---|---|
| es2abc | 4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca |
| shared.js | 7610981f9c1aabe0516d1e038762d96129e919e1099a832a7e7d3a9e98f82a9f |
| shared.abc | a181188d4573d4928fe2500c4892c3ca6d67c90f18183906d410617b4a782227 |

`shared_terminal_cli_test` compares 1,440 observations per contract flag set:
all dispatch/boolean choices, changing predicate results, getter/call failures,
return values, exception constructors, catch effects and exact receiver/order.
Erased returns, doubled effects, swapped arms/tails and a corrupted catch payload
must be observed. The independent unit verifier rejects forged original edges,
owners, follows and normal exit ports. Exact 200/201/400/800 work/byte boundaries
are tested.

Default/O0 recompilation via `tests/tools/grouping_bytecode.py`: `classify` and
module-entry method are exact; dispatch methods differ by the independently
verified branch inversion/arm layout and consequent register, label, feedback
slot and literal-address allocation. This is not reported as strict bytecode
identity; see `docs/design/source-recovery/control-shared-terminals.md` for the
comparison boundary. No source is fed back to the decompiler.
