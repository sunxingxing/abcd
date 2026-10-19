# Cell lifetimes across a native for-of

These are independent ordinary-source oracles; the engine sees only ABC.
`callback.js` publishes native for-of with a noncaptured scalar key, a captured
function-local accumulator, and a helper passed through the body callback.
The callback invokes the helper, so captures remain live/mutable across
iterations and outside the loop. Node compares 34 engine/original cases:
zero/multiple iterations, continue/exhaustion, body throw, return getter/call
failures, primitive/null/missing return, next/done/value getter failures,
receiver identity and helper identity/isolation. A changed-accumulation output
mutation must be observed. Engine output also passes `node --check`.

The remaining fixtures intentionally assert the real next typed gate:

- `cells.js`: direct reads/writes to the captured cell inside the loop, through
  a local helper. Lifetime analysis verifies; recovery still refuses at
  TreeStructureError::kOwnership / TreeObligation::kForOfRecovery.
- `skip.js`: explicit same-loop continue, same downstream recovery refusal.
- `stop.js`, `finish.js`: native break/return close shapes whose original close
  ownership is not yet sealed; ExceptionControlError::kMultipleExits stays.
- `throwing.js`: explicit body throw reaches the existing evaluation ownership
  gate. It is not silently folded into the successful callback-throw case.

These negative fixtures are not counted as successful engine differentials.
This slice changes no close/iteration/recovery admission to make them pass.

The unit test imports the real `cells.abc` into original exception SSA, seals
close ownership, builds the evaluation/emission view and independently checks
cell lifetimes. It tests missing seal, changed port/payload, wrong LoopId and
forged declaration allocation, source-loop ancestry and absent close-block
scopes; register/copy/block-split variants and exact 200/201/400/800 budget
boundaries also run. Existing phased-initialization tests retain scope-escape
and catch-state rejection.

`manifest.json` records the pinned DevEco es2abc path/hash and source/ABC hashes.
For each source in this directory the command is:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output NAME.abc NAME.js
```

Node does not verify shared-heap/cross-thread behavior or the V4 exception-Hole
premise; these fixtures use ordinary language values/objects.
