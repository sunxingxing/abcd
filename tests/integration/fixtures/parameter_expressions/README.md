# Parameter-initializer expressions

Real dynamic es2abc inputs. Only ABC goes to the engine; JS is an independent
Node/compiler oracle. `later` deliberately reads a later parameter and must
refuse parameter-scope recovery. The other six fixtures must publish.

Compiler: DevEco SDK `ets-loader/bin/ark/build-mac/bin/es2abc`, SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
For each stem, run from the repository root (replace STEM literally):

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --extension=js --opt-level=0 --record-name=STEM --output=tests/integration/fixtures/parameter_expressions/STEM.abc tests/integration/fixtures/parameter_expressions/STEM.js
```

The integration test separately recompiles original and emitted JS with default
optimization and O0, preserving record names, options and source-file names.
The identifier export alias adaptation is applied to both sides. No register,
branch, exception or method-body difference is normalized.

| ABC | SHA-256 |
| --- | --- |
| captures.abc | `c71ee147f0828121d1d4e0374b236f0632b3f2376f97966b0643c8c1313a1c27` |
| earlier.abc | `c47ee2c5451b3f09e58d83835f1de5cdd463e8023a58228080f6a3bbdf5fd665` |
| functions.abc | `e062e8a7ee14b973bb831d6757e8e155c37a8942318556073659ec3087473455` |
| later.abc | `b9f2274faae520d380dc9ee86a1264ae5cb482aad83c46ac3a63ee06c59cf2ea` |
| method.abc | `a48545399624ddd049f31539abd4bc7b2d6c1f62e0f5dc2fab30cdf0e179da7b` |
| throwing.abc | `f3c3286649cc085281a89aefa98b3f0495c65353a2eaa4aa1f266ca1491a5a12` |
| updates.abc | `0478e82a14a671db962044c295684b867f1aa479fc30f47c46d13b163879bfa6` |

`updates` verifies reassignment of the captured initializer cell and isolation of
separate creator calls. The module oracle also reassigns its live export between
calls; no snapshot of either binding is accepted.
