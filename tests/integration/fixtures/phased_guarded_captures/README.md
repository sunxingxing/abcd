# Phased guarded capture reads

Independent ES module source oracle and real DevEco es2abc output. These inputs
exercise a captured named hole-check covered by the child function's handler,
with its parent creating the function before the captured `let` store.

Compiler: `/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`, ABC 24.0.0.0.
From the repository root (substitute `read`, `forward`, or `write` for NAME):

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=NAME --source-file=NAME.js \
  --output tests/integration/fixtures/phased_guarded_captures/NAME.abc \
  tests/integration/fixtures/phased_guarded_captures/NAME.js
```

Verified with `abc_semantic FILE METHOD --instructions`:

| File | Method | Original input evidence |
|---|---:|---|
| read | 395 | `ldlexvar` PC 23, named hole guard PC 25; guard dispatches to handler PC 106 |
| forward | 462 | `ldlexvar` PC 10, named hole guard PC 12; guard dispatches to handler PC 23; the middle function 406 forwards without a lexical read |
| write | 384 | guarded read PCs 13/15, unguarded `stlexvar` PC 25, guarded read PCs 27/29; handler PC 40 |

`phased_guarded_captures_cli_test` compares the engine's source with these JS
files under Node using both numeric+constructors and language-source contracts.
It checks before/after-initialization values, exception constructor
(`ReferenceError`), catch local-state staging, finally log order, and forwarding.
It detects declaration hoisting, deletion of the checked read, and corrupted
catch staging. `write` must retain the strict Language capture requirement and
refuse with `CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization`.

Message boundary: es2abc lowers native lexical TDZ reads to the same throwing
hole-check protocol. The Node oracle compares exception constructors and
control/value/log traces, **not** error messages or stacks, and does not claim to
execute the ABC in Ark. Recovery IR tests independently check exception and
success-value correspondence, including mutations of the projection.

SHA-256:

```
5c17d2cd7a04f051b021a94ef7aafabd99866aa179d53b98319bab72bbd43cdc  forward.abc
4dec819560f5177ab1e6985098ffab2985e7e1c8cc4f80b45d5c446672e0a4d1  read.abc
09388f5ba0aab304a0d3a52c0115cabb3546f0672dbaf3ba709284404a0d9f76  write.abc
```
