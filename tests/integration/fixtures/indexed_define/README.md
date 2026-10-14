# Round 50 indexed_define

Sources in this directory are independent Node evaluation oracles, never engine inputs.
CALLRUNTIME_DEFINEFIELDBYINDEX (0x02fb); phased definitions have real handlers. The oracle uses object literal data definitions ([[Define]], all flags true), not assignments. Inherited setters/readonly properties cannot intercept initialization. Getter/RHS exceptions retain order.

Compiler: DevEco es2abc, SHA-256 `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name=normal --source-file=normal.js --output normal.abc normal.js
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name=phased --source-file=phased.js --output phased.abc phased.js
```

The integration test asserts the target instruction and exceptional dispatch before comparing Node executions. Both flags are checked. Exception constructor and full observable traces are compared; message/stack text is not. There is no finally-throw source-oracle claim. The tests do not execute the pinned Ark VM.

SHA-256:

- `normal.abc`: `07c5c87041cafd6db9ac39dce4e8bc08edd75af056ec5ae647f83ba203456d13`
- `phased.abc`: `669d82b516c448a798b68ffba021e83c81a5a311ea0e1ff75de621df58284a02`
