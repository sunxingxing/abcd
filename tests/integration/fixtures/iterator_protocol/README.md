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

## Round 79 A2: correlated completion relation

`completion_relation_abc_test` reuses this exact ABC (it is not regenerated).
It checks all raw may-exit tuples of m423's break close (PC 87..158), m423's
return close (178..249), and m451's original-throw close (76..134). Expected
relations contain 7, 7, and 2 tuples respectively, with original edge, payload
and handler/caller target identities checked individually. The last case
retains the original throw payload after the inner return getter/call Catch.

This remains a raw abstract relation under explicit entry-completion premises,
not a native IteratorClose certificate or source-publication test. Unknown
Hole comparisons retain both paths, including an extra possible Hole rethrow;
all original exception exits remain. The next slice must prove entry, guard,
priority, activation and ownership obligations before consuming it. No Node
oracle or cross-runtime equivalence is claimed by this test.

## Round 79 A3a: derived close entries

`close_entries_test` derives the original edges and pending completions instead
of supplying them as A2 API assumptions. It checks m423's Break/Return entries,
m451's body-Throw entry, and m423's six current exceptions entering the outer
close after an earlier close failed. Known edge-qualified state narrows the
latter to one rethrow per entry. Unproved Hole comparisons remain conservative.
The same ABC is reused without regeneration. See
[`close-entries.md`](../../../../docs/design/source-recovery/close-entries.md)
for the independent boundary verification, exact-budget tests and the remaining
ownership/priority boundary. No production hook or Node oracle is added.


## A3c: outcome-indexed close relation

`close_outcome_test` reuses the unchanged compiler artifact and hashes above.
No additional fixture is necessary for the five outcomes: `xs` is opaque, so
m423's same break/return close code covers a throwing return getter, throwing
return call, primitive result, null/missing method and object result. The unit
oracle checks ORIGINAL events and identities, not paired application source:

| close | return read | call | nullish branch | result check |
|---|---:|---:|---:|---:|
| m423 Break | 105 | 121 | 117 | 150 |
| m423 Return | 196 | 212 | 208 | 241 |
| m451 Throw | 96 | 112 | 108 | unreachable check at 127 |

A2's may-exit projection is retained exactly. In m423, unresolved Hole
comparisons can still take a spurious completion arm. These paths retain their
outcome plus a typed uncertainty witness and cannot pass priority. Likewise the
raw comparison exception exits remain unproved (m423 PC114/205). Getter/call
errors are always identified by their own original exception exit, never by the
pending Return value or original body Throw. Mutation tests reject changing
those identities, swapping outcomes, calling a primitive result successful, or
clearing uncertainty. This is symbolic raw-ABC analysis, not execution of ABC in
Node and not a new source-publication or native-for-of acceptance claim.
