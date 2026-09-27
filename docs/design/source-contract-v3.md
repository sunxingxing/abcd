# Source Contract V3

Approved by the user on 2026-09-27: recovered source text may differ, while
execution behavior, scope, parameters, return values and side effects must
remain correct. V3 extends V2 only in this textual dimension.

## Scope

`SourceContract::kLanguageAndHostV3` is the default whole-source-engine contract.
`CallableText(V3)` is `kRecovered`. Function.prototype.toString may expose the
recovered spelling, formatting or a proved alternative callable syntax rather
than the original text. Exact source text is not reconstructed from sidecars or
source maps and is not a completion requirement.

This is NOT permission to change callable behavior. In particular it does not
relax lexical/dynamic this, captured bindings, strictness, parameter defaults,
arguments contents, function name/length, constructibility, evaluation order,
object identity, return values, exceptions or observable side effects. These
continue to need the same independent proofs. Source-text-dependent application
logic is not proved equivalent merely by approving different text: consumers
that evaluate or otherwise depend on original text need their own analysis.

V2 premises remain unchanged: lawful JS/ArkTS and host values on normal returns
and exceptions, stable unshadowed realm globalThis, and excluded VM resource
exhaustion differences. V3 does not supply any new host capability, assume a
non-Proxy superclass, admit malformed bytecode, waive the decompiler's memory
cap, or imply support for any additional instruction/protocol.

## Integration

- Contract identity is typed and remains part of route input identities and
  receipts; serialized label is `language_and_host_v3`. V1 and V2 retain their
  old enum values and serialized meanings. Unknown labels still reject.
- Evidence rules advance to 43. Existing whole-source producers inherit V3;
  explicit historical identities are not silently upgraded. Future cache keys
  must continue including the contract and evidence rules.
- Arrow invocation carriers consult each tree's identity at destination checks.
  V3 supplies the text permission, so no additional GlobalScope text flag is
  necessary. V2 still needs explicit `CallableTextPolicy::kRecovered` for that
  path. An unspecified destination flag cannot revoke the immutable V3 contract.
- Host argument-snapshot binding remains mandatory. Callable ABI, scope,
  environment, use/def, tree structure and publication checks are unchanged.
- Position JSON records the contract for every tree, including ordinary
  callables; carrier metadata still records its invocation form and recovered
  text policy. Source text is never parsed back to infer this contract.

## Verification

File-backed creation tests cover default V3 arrow publication with a host
snapshot binding, V2 refusal without the extra text premise and V2 admission
with it. Existing missing-host, receiver, new.target, callable-form and scope
negative cases remain. Generated-JS tests verify callable name/length,
constructibility, current argument slots, extra tail, defaults, branches,
loops, self identity and PC mappings under V3. Receipt tests round-trip all
three versions and reject unknown versions; policy tests check that the other
V2 premises are unchanged. This is not an Ark VM differential or corpus-wide
equivalence result.
