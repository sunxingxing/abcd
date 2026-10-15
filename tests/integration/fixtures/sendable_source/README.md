# Closed Sendable source (Round 74 II)

`closed.ts` is independently authored evaluation source. Only its ABC is an
engine input. It has one NUMBER storage description, no initializer, and two
ordered constructor [[Set]]s. The engine must publish a complete `.ets` record
with `source_language: arkts`, not an ordinary class erasing Sendable semantics.

Compiler: DevEco `es2abc`, ABC 24.0.0.0:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`.
Binary SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Pinned frontend/runtime identities: `docs/reference/openharmony-v7.0-release.lock.json`.

From the repository root, with COMPILER set to that path:

```sh
$COMPILER --module --merge-abc --extension=ts --opt-level=0 --record-name=sendable_closed --output=tests/integration/fixtures/sendable_source/closed.abc tests/integration/fixtures/sendable_source/closed.ts
```

Do not use `--use-define-semantic`. `.ets` requires the explicit `--extension=ts`
parser selection in this compiler. The storage descriptions have **no
initializer**. The engine retains both constructor assignments; it must not
replace them with initialized class fields or `Object.defineProperty`.

`sendable_source_cli_test` first exports the real ABC through the engine and
recompiles the emitted record with those same options and record identity. An
independent ABC probe compares class kind, constructor length and serialized
flags, heritage, member names/kinds/length/placement, field names/masks/order and
instance split, constructor [[Set]] order, sendable environments, exports and
module requests/shared flag. The real SendableTest corpus record is compared
in the same way. Missing directive, changed mask, removed assignment and added
field initializer are discriminating round-trip mutations.

The Node differential erases **storage-only** TS descriptions and compares
ordinary construction, reads/writes, setter traces and setter-thrown exception
constructors against a separately authored JS oracle. It detects removal of a
constructor assignment. It does not test Sendable heap restrictions, field-type
enforcement, cross-thread behavior, or shared identity. Those are explicit
runtime-verification gaps, not evidence that erasure is authorized. In particular,
erasure does not model preallocated shared own slots or their prototype lookup.

The CLI test also compiles 200/201/400/800 fields. The C++ test exercises real
production discovery/preparation/finishing, fixed-size proof mutations and exact
byte/work admission boundaries. GENERIC stays a located `kTypeSpelling` refusal.

SHA-256:

- `closed.ts`: `b9e2d38001bd111d318b1eff651239564f3d72ee49e08206f76908d95978848f`
- `closed.abc`: `3767532cf51c5bd5aff46a35c194666f7680e21326695a229872b628f2c072e9`

Additional negative evidence: a temporary copy with mask 256 refuses only its
owning class (field ordinal/mask/leaf cause and original/working PC retained),
while unrelated complete records keep identical source. Quoted import/export
wire names without a spelling accepted by this pinned TS parser stay a typed
record kBinding refusal with no fabricated PC. Static initialization remains a
preparation gate; it is not synthesized or omitted. Primitive/union instance
fields and quoted/contextual-keyword field names have actual round-trip tests.
