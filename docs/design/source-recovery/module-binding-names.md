# Anonymous module bindings

The common record/function route derives source module spellings from ABC
metadata, never from paired source. `ModuleSymbol` owner and slot remain the
runtime identity; emitted spelling is a separate boundary decision.

The pinned v7 frontend (`arkcompiler_ets_frontend` commit
`c754cf6300873cdd8cdb71c7340a98abfd885389`, Apache-2.0) defines
`SourceTextModuleRecord::DEFAULT_LOCAL_NAME` as `*default*` in
`es2panda/parser/module/sourceTextModuleRecord.h`. The default-export emitter
looks up that binding in `ir/module/exportDefaultDeclaration.cpp`. This wire
sentinel is not a JS identifier. It receives `NameEvidence::kSynthesized`;
ordinary identifier spellings retain metadata evidence. Other invalid names
remain unsupported, rather than being silently sanitized.

`ResolveModuleBindingNames` groups adjacent equal decoded local-export names
into VM slots. Equal nonadjacent groups stay distinct. Public export aliases
still share the original slot and native ESM live binding. A generated name
uses an output-only `$default` prefix, one more `$` than the longest immediately
following run in any import/local name, then the slot number. The complete
metadata inventory determines spelling, so sibling methods reading different
subsets agree and generated names cannot collide with inventory names.

Record stores and normal/phased module readers use that same adapter. TDZ
guards are verified against the original wire name before renaming. Renaming
does not prove initialization, remove a guard, widen read permission, change
callable name inference, or authorize a detached local module read. Record
assembly independently checks the actual tree's name/evidence/access/owner;
destination verification also checks the explicit declaration evidence and
intervening scopes. The historical empty evidence span retains the metadata
premise, not blanket permission for synthesized bindings.

The implementation is local C++ code, not copied upstream code; no dependency
or license change. Entity decoding and decoded-text equality are interned per invocation. See
algorithms.md for dimensions and scaling. This is not a new module scheduler
or cross-function cache; the existing per-function module-reader costs remain.

Regression evidence includes complete decoded records in profiles 1202, 1206,
1301, live imports and aliases, colliding user locals, and a nested function
reading its own exported binding. Normal/phased reader fixtures retain guard
and owner/access/name/evidence negative cases. The long-name alias inventory
test catches repeated decoding that would grow quadratically with input size.

The paired Logger oracle reads reference TS only in tests and compares 240
scenarios per Logger record (Pasteboard and MultiCommunity) under a declared
hilog mock. Its reference erases each sample's two
uninitialized type-only TS properties before Node type stripping, reflecting
this sample's assignment-field emission. It does not infer that all Ark
profiles erase fields: v7 compiler/core/function.cpp explicitly distinguishes
`UseDefineSemantic`. This is not an Ark VM or real hilog differential result.
Native method/rest representation is checked separately; the remaining class
carrier, aliases and discarded expressions do not yet establish whole-record
source-style acceptance. The separately paired CardArrayViewModel oracle covers
24 cases at 0/1/2/200/201/400 list sizes, arrow lexical receiver despite a
different callback receiver, item/index order, thrown constructor identity and
cutoff, exported singleton, fresh arrays/items and the named class export.
It also declares its mock-host and unmodified-intrinsic scope; it is not an
unrestricted field-define/field-assignment or Ark VM equivalence claim.
