# Pre-control iterator candidate fixture

Source: `main.js` in this directory; no paired application source is an engine
input. Compiled on 2026-10-14 with the local DevEco SDK compiler:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 \
  --output tests/integration/fixtures/iterator_protocol/main.abc \
  tests/integration/fixtures/iterator_protocol/main.js
```

SHA-256:

- Compiler: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`
- `main.js`: `28051e6eaa6e222908f0d06f7adbaa9570ea1504bf5f890794c798cc127476b4`
- `main.abc`: `15bdac19ded47f78cf66789764829ed98361d3fcc29c0331bd01c75ec15dfd70`

The test checks actual getiterator instructions, resolves next/done/value/return
pool keys, imports the original exception SSA and checks candidate identity and
return-read dispatch associations. Methods 395/423/451 have 2/1/1 acquisitions
and 6/4/2 region associations respectively. These counts include separate close
occurrences; they are not counts of owned regions.

This is a candidate-analysis test, **not** a close-priority or source-publication
test. Break/return/throw/nested syntax makes real expanded close code available
for further verification, but the current test does not establish that its
completion selection implements native IteratorClose. It performs no Node
differential and grants no protocol region ownership.
