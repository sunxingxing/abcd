# Synchronous concurrent functions

Source authored for recovery tests, not copied from an application. `functions.js`
uses the dynamic frontend's body directive. It covers explicit and implicit
returns, an uncaught throw, catch/finally, a live imported binding and an import
read forwarded through an ordinary child. Two dependencies exercise distinct request identities and repeated request
elements in one method. The companion dependency changes its
export between calls. Named exports expose the functions for the harness and test module-table round trips. Concurrent output uses IdentifierName aliases for the
pinned parser; no test-only source rewrite is performed. Engine output, not this
source, is executed by the test.

Compiler: DevEco es2abc at
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`.
SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```
es2abc --module --merge-abc --opt-level=0 --record-name=concurrent_functions \
  --source-file=functions.js --output=functions.abc functions.js
```

ABC SHA-256: `ad406f0c599780c803960296a488ecff682caf5c508ac728252f0b7fba1167f4`.
Source SHA-256: `8047091253ebcbafdbb1063cbdf48639ef77ed3964229364b6b206688b8fafee`.

`concurrent_roundtrip_probe` reads method attributes, request annotations and
module tables directly. The CLI test asserts all six methods have concurrent
kind, sendable=false, a present (possibly empty) annotation and raw notification
instructions. `handled` has real handlers. The output is recompiled with the
same options and record identity; request comparisons use specifier identity,
retain duplicates/order and distinguish a present empty annotation from missing.
A test probe imports each recompiled concurrent method into its original phase
graph and re-derives the completion proof (under the same source numeric
premise; the raw protocol probe is not source publication), rather than
requiring raw return/notification counts to match across duplicated dead
implicit epilogues and finally copies.

Node executes ordinary calls, checking values, exception constructors, effect
traces and live import changes. It does not validate taskpool scheduling,
notifications to the host, cross-thread/module initialization or shared memory.
No finally-throw mode is compared with a JS source oracle: the compiler's
protected inlined finalizers can differ from native JS (see exception recovery
documentation). A result mutation must be detected. Directive erasure is tested
through recompilation kind/metadata, which Node cannot observe.

`forwarded` recompiles with STOWNBYVALUEWITHNAMESET before its notification, an
operation not yet source-lifted by the engine. For this handler-free method the
test-only suffix oracle uses the decoder's register metadata plus register SSA
and entry dominance: the notification and return read the same SSA value, all
paths after notification are effect-free and terminating, and there is one
physical notification/return port. It does not supply heap semantics or grant
source publication. The other recompiled methods use full phased import and
completion re-derivation. This distinction is retained in probe JSON.

`unicode.js` compiles with the same options, `--record-name=unicode_concurrent`,
`--source-file=unicode.js`, `--output=unicode.abc`. SHA-256:
`unicode.js`: `1831ff0b5df368153b50590e31ab76d811809cd033ecec46333be4c875a57a15`;
`unicode.abc`: `fa40c58a0ff6accfeba1e43c88aca44351c7f05ab7258dc18969178e30dd4d03`.
It is an intentional typed `kModuleSyntax` refusal: the current module writer
spells non-ASCII imported names as string names, which the pinned parser cannot
recompile. No broken concurrent artifact is published. This is a formatter
coverage gap, not a restriction on ArkTS Unicode identifiers.
