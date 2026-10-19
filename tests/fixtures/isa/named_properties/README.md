# Named own-definition / function naming fixture

The engine receives only `names.abc`. `names.js` is the independent Node oracle.
The fixture contains functions, arrows and a class under constant string keys,
ordinary identifier-key functions, sibling getter reads, and 100 named members
to exercise the imm16 cache encoding. Keys containing a dot and backslash make
the pinned dynamic frontend emit STOWNBYNAMEWITHNAMESET instead of supplying the
name at DEFINEFUNC creation. The integration test asserts both decoded widths.

Compiler: `/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`

Compiler SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

From the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output=tests/fixtures/isa/named_properties/names.abc tests/fixtures/isa/named_properties/names.js
```

SHA-256 hashes (also asserted by `name_setting_literal_source_test`):

- `names.js`: `c461b8049a34a241da909f879712b64f767fc16ccad5d62077895df1a2a40e6d`
- `names.abc`: `5211460b53978ecf7de6489704c1bc823b6e11f0f35e77d9436fbb669e4f169b`

The Node differential runs the actual engine exports under both contracts. It
checks function/arrow/class names, own property and name descriptor flags,
construction/calls, sibling getter order and each getter throwing, and that an
inherited setter is never invoked. A mutation introducing explicit function
names must change the observed result. A separate family printer mutation
rejects an explicitly named child or a non-function property value.

No computed-key projection is tested or admitted: the numeric-key fast-path
name difference remains gated. Classes with static-initializer timing are not
admitted by this proof. No Ark VM execution or cross-runtime claim is made.
