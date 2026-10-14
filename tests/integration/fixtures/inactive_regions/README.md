# Inactive exception regions

Real DevEco es2abc fixture. Engine inputs are ABC only; `inactive.ts` is an
independent Node oracle. Generated on 2026-10-14 with:

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name inactive --output tests/integration/fixtures/inactive_regions/inactive-o0.abc tests/integration/fixtures/inactive_regions/inactive.ts
"$compiler" --module --merge-abc --record-name inactive --output tests/integration/fixtures/inactive_regions/inactive-default.abc tests/integration/fixtures/inactive_regions/inactive.ts
```

SHA-256:

- compiler: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`
- source: `0412ac62c1a6c425fba455b2e2012eb0de188e22b0e82261f6501d50a0abf3ed`
- O0 ABC: `c1f30fe02557cceeeb2dd2c72829fedfef6f770b36558e5c39b7e2de362d12bb`
- default ABC: `c12e253ff1dffc9691c88383939989f533dc01d6a307040c7643cf0ead40d940`

The test decodes real try tables and original semantic exception flow, requiring
two inactive regions and one active region in the O0 binary. The default
optimizer emits zero-length catch extents in all three methods; the test
requires the existing typed kHandlerShape refusal (with region/method/PC
evidence), rather than guessing a handler end. Node comparison uses O0. `passthrough` has
only register transfers and return in its protected range (O0 PC 8..15,
handler PC 17); `split` additionally has a conditional. `active` has a throwing
property access and must keep its catch. These are fixture assertions, not
production opcode templates. Node compares primitive return values, both
branch outcomes, getter effects, and recovery of a thrown TypeError. Removing
the real catch's recovery must fail. No finally-throw or Ark/Node message
identity equivalence is claimed.
