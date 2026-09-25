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

Published artifacts now select `language_and_host_v2`. Source-owned normal and
exception SSA analyses explicitly pass `CallResultValues::kLanguage` to the
shared typed value rules. Ordinary calls then produce language values on the
normal path/Commit only. Input and explicit-return validation, module/capture
proofs and pending-exception handling remain independent. Raw analysis defaults
remain unknown; non-default call premises require typed IR. This adds constant
work per existing value rule, with no new graph or per-instruction storage.

LDGLOBAL uses `globalThis` under the V2 premise unless an explicit host binding
is supplied. The printer still checks its own generated scope, including
function/class names and parameters, for shadowing. Validation is cached once
per immutable printer scope, not repeated for every LDGLOBAL. The versioned
artifact contract records the premise; no source text or opcode-name dispatch
is used to infer it.

`ResourceFailureScope::kExcluded` gates a retained leading lexical allocation
in the supported modern async prologue; see `async-lexical-prelude.md` for its
independent proof boundaries. The rejected superclass invariant is unchanged. These decisions
do not assert complete pasteboard coverage or runtime equivalence. Tests, JS
execution and RSS measurement remain paused; call-result/operand, exception
Commit, realm-shadowing and V1-default regressions remain deferred.

The explicit `HostRealmBinding` API is implemented independently and remains
useful for embedders that supply their own immutable realm-object binding.
No CLI option fabricates this capability from a bare identifier. The V2
`globalThis` policy is a distinct explicit contract, not a fabricated host
lexical binding, and does not populate `host_realm_binding` metadata.

## ABC-Only Integration Evidence

Both production CLIs built successfully. Exporting the unchanged pasteboard
`modules.abc` (SHA256
`854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e`)
through `abc_structure --shared-source` to the untracked
`build/pasteboard-contract-v2-01` directory produced seven incomplete app
records, up from three: CommonConstants, Logger, Home, PageA, PageB, PasteBoard
and ShareDitect. The four newly published records use the shared source path,
not a new record-specific writer. PasteBoard contains recovered imports,
exports, async methods/arrows and registration's `globalThis` expression.

The remaining app records refuse at outer PCs 14 (EntryAbility), 14
(EntryBackupAbility) and 264 (ProgressBar). Nine metadata records have no module
source. All seven source artifacts remain explicitly `source_complete:false`
and `executable:false`; mapping still covers entry positions rather than every
nested method's final-file positions. Register temporaries and generated labels
also remain. This is source-generation evidence, not JS execution, full source
equivalence, source-style completion or memory-limit evidence.
