# Round 50 super_read

Sources in this directory are independent Node evaluation oracles, never engine inputs.
LDSUPERBYNAME (0x46 or 0x92); phased reads have real handlers. The oracle retains the native class home object. It covers instance/static receiver identity, parent changes, Proxy parent Get, null parent, getter/argument/call throws and callee changes during argument evaluation. No cached-parent or __proto__ translation is accepted.

Compiler: DevEco es2abc, SHA-256 `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name=normal --source-file=normal.js --output normal.abc normal.js
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name=phased --source-file=phased.js --output phased.abc phased.js
```

The integration test asserts the target instruction and exceptional dispatch before comparing Node executions. Both flags are checked. Exception constructor and full observable traces are compared; message/stack text is not. There is no finally-throw source-oracle claim. The tests do not execute the pinned Ark VM.

SHA-256:

- `normal.abc`: `dc93e5ec8c79fe2f3066b3fa6ad12508de60c6d103c7bf6867aba4e758ac76cb`
- `phased.abc`: `ab999244d676c11eb6a219f043999054fc4c05f3eee4082f021be7f7cc282736`
