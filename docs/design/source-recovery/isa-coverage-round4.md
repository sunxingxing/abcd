# ISA round 4: private object-spread construction

The implemented part consumes `ObjectSpreads()` (LanguageSourceV7) in the
ordinary literal pipeline. It does not add a language premise. Tagged templates,
object rest, and the remaining property/throw families are not claimed complete.

`CopyProperties` models both current and deprecated encodings. The destination
register and source accumulator/register are distinct read roles. The successful
accumulator definition has the destination's domain, including Hole when a raw
input permits it. It is not seeded Language. The operation retains heap read,
heap write, user-code and allocation effects and a Before/Commit exception exit.
References: pinned `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:5464`
and `ecmascript/stubs/runtime_stubs-inl.h:453`.

The identity index includes the **normal result equation** result = destination.
This equation never permits effect elision. Evaluation retains the spread as a
literal definition, and verifies the canonical identity independently. LiteralPlan
proves the fresh ordinary allocation, ordered normal commits, nested operand
ownership, exception correspondence and absence of observation/escape before the
last definition. The existing all-path effect order and copy/phi identity indexes
are reused. No per-construction CFG walk or opcode sequence admission is added.

A DataProperty with absent static key represents an object spread; there is no
invented NameId. Only object literals under V7 accept it. Tree operand verification
requires the original spread, fresh LiteralPlan and matching destination/result
identity. Recovery owns a separate ObjectSpread action for each property; later
flow refinement must not use the aggregate's allocation-only effects to erase
that action. Discarded spreads remain observable. Buffered static properties may
be merged before a spread, but no later definition is moved ahead of it.

The SDK fixtures cover interleaved/multiple spreads, nested objects, overwrite
order, null/undefined and primitive sources, symbols, getter/throw order, inherited
setters and a discarded result. Node compares source against engine output. The
approved Ark accessor-copy difference is recorded explicitly in occurrence
provenance; a Node pass is not an Ark execution claim. Constructed fixtures also
exercise both encodings in profiles 12.0.6 and 24.0.0. The independent literal
verifier has missing/reordered definition, wrong target, missing result, forged
property, origin and premise mutations, plus 200/201/400/800 copy/block-split
variants and exact work/memory admission boundaries.

## Object-rest boundary requiring separate treatment

The approved V7 object-rest difference is Set versus CreateDataProperty. The
pinned main stub additionally unwraps a JSProxy before copying its target:
`runtime_stubs-inl.h:2642-2648` calls `GetSourceTarget`. Native destructuring rest
uses the proxy's ownKeys/getOwnPropertyDescriptor/get operations. For an unknown
input, bypassing those traps is an additional observable difference, not covered
by the approved Set premise. This round does not assume that difference away.
A future premise-free implementation can start with an independently proved
non-Proxy source domain; arbitrary input needs a separate user decision. The
excluded-key identity/order and original coercibility guard are still required.

## Remaining work

Tagged templates need a dedicated source node and a construction/call event
certificate, including raw/cooked pairing, undefined cooked entries, tag receiver,
substitution order and per-site occurrence identity under the approved V7
fresh-array difference. No generic call or array alias is treated as that proof.
Super stores need retained home/base/receiver and RHS order; RegExp needs exact
pattern/flag validation before making executable bytes into literal syntax;
proto and intrinsic throws need their own source/reference correspondence.
Generator/async-generator expansion is not implemented in this slice.

## Verification

- Full build passes; source-regression 65/65, scale 11/11, ISA 15/15,
  object-spread targets 3/3. Initial overlapping runs hit missing generated
  fixture artifacts and a VM timing failure; isolated reruns pass all 65.
- Required area sweep: 209/221. Remaining failures are pre-existing:
  family_source_cli, tree_structure, try_tree, literal_construct, mixed_literal,
  phased_literal, class_tree_source, literal_closure_source and unguarded peer,
  derived_literal_receiver_source and unguarded peer, try_tree_source. The
  additional all-source sweep completed 484/495 tests before being interrupted
  at unrelated navigation-graph shards; it is not counted as a passing suite.
- Cohorts versus fdf1152b remain source 605/620 and previous 542/620, zero new
  completions/regressions/text changes. All 80 manifests also compare exactly.
- Peak decompiler RSS over 80 sequential runs: 42,434,560 bytes (Darwin wait4).
- At 800 spreads, the compact normal graph retains 3,600 bytes of certificate,
  uses 114,674 work and admits at exactly 296,802 bytes. The copy/block-split
  graph retains 3,600 bytes, uses 461,234 work and admits at 1,403,736 bytes.
  One byte/work unit below the tested boundaries refuses rather than proving.
- Six complete fixture records, 118 Node cases and six syntax passes. The
  occurrence producer imports 230 Node rows, 120 round-trip matches and 110
  unmatched rows (left null). There is no Ark VM execution claim.
- `grouping_bytecode.py` reports, separately at default and O0, 4 exact records,
  2 different, 0 unverified. All 48 named method bodies across both modes are
  byte-for-byte equal. The two SDK files differ only in the literal table's
  order/addresses (the emitted module sorts its function definitions). Literal
  contents, instructions, operands, handlers and module metadata are unchanged.
  The compiler input adaptation expresses reference function exports as
  equivalent named-export clauses, then applies the existing identifier-valued
  string-export adaptation equally on both sides. The grouping-only token
  checker rejects all six recovery pairs because declarations/names change;
  these rejects are reported, not counted as semantic failures or passes.
- Ledger: 132/62/110/28 -> 134/62/108/28, 902 measured ABCs, no probe failures;
  current/deprecated COPYDATAPROPERTIES promote under profiles 4 and 6 only.
  No variant or profile moves backwards. Full JSON and occurrence artifacts are
  under `build/review/isa-r4`; validation logs, method comparison, diffs and
  resource measurements are under `build/review/isa-r4-work`.
