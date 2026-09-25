# Source Contract V2 Decisions

The user approved the following source-equivalence scope on 2026-09-25:

- Legal JS/ArkTS and compliant host APIs may throw any JS value, but not
  VM-private tagged markers (the existing V1 premise).
- Normal JS/host call results are also JS values, not VM-private markers.
  Explicit ABC return operands and all instruction inputs still require their
  independent value validation.
- `globalThis` remains the realm's original global object for the application
  lifetime and is neither rebound nor lexically shadowed. This latest approval
  supersedes the earlier refusal of that premise. It does not authorize
  arbitrary source identifiers as substitutes for the realm object.
- Differences caused by VM-internal resource exhaustion (OOM, stack exhaustion)
  are outside source equivalence. Application and host-API exceptions remain
  observable. The decompiler's own 1,000,000,000-byte memory limit is unchanged.

The user rejected an unrelated lifetime JSFunction/non-Proxy premise for the
EntryAbility and EntryBackupAbility superclasses. Those constructors must stay
refused unless a separate sound implementation or independent proof exists.
The global-object approval does not imply superclass immutability.

## Rollout State

These are approved decisions, not claims that all corresponding paths have
already been admitted. Current published artifacts still select
`language_and_host_v1`. The next integration must introduce a new version,
explicitly pass typed normal-result premises to source-owned value analysis,
select the approved realm binding policy, and gate lexical async prologue
admission on the resource-exhaustion scope. Raw analysis APIs must keep their
conservative defaults. No opcode name, diagnostic string or host API name may
substitute for an explicit policy selection.

The explicit `HostRealmBinding` API is implemented independently and remains
useful for embedders that supply their own immutable realm-object binding.
No CLI option fabricates this capability from a bare identifier. Implementing
that API alone does not activate the new default contract.
