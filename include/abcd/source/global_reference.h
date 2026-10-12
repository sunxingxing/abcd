#ifndef ABCD_SOURCE_GLOBAL_REFERENCE_H_
#define ABCD_SOURCE_GLOBAL_REFERENCE_H_

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include "abcd/source/source_contract.h"

namespace abcd::source {
class ModuleEnclosure;
enum class NameEvidence : std::uint8_t;
// Embedding-host trust boundary, not evidence inferred from a name or ABC.
// Host supplies an initialized immutable lexical binding to this ABC realm's
// original global object, visible for the generated code's entire lifetime.
// The borrowed name must remain stable during synchronous source emission.
struct HostRealmBinding {
  std::u16string_view name;
};
// Host-proven immutable lexical binding to this ABC realm's original Symbol
// constructor. The public Symbol property is not this capability, even under V2.
struct HostSymbolBinding {
  std::u16string_view name;
};
enum class ArgumentSnapshotAbi : std::uint8_t { kV7CurrentSlots };
// Immutable lexical factory binding. Called with (unexposed native arguments,
// dense declared-slot array). For each actual argument it copies the current
// slot if declared, otherwise the original tail. It returns a fresh v7
// JSArguments object with exact realm prototype/iterator/thrower descriptors.
// It must not mutate, retain or expose either input, invoke user code, or throw
// except resource exhaustion excluded by the source contract. No ABC inference
// or arbitrary CLI name can certify this embedding-host capability.
struct HostArgumentSnapshotBinding {
  std::u16string_view name;
  ArgumentSnapshotAbi abi;
};
struct GlobalScope {
  // Caller-proven complete ASCII outer source bindings, sorted by byte name;
  // non-ASCII names cannot collide with admitted ASCII global references.
  // Excludes imports owned by import_names/module_enclosure, which are indexed
  // separately; includes intervening wrappers that could shadow those imports.
  // Build once per record, never rescan per method.
  std::span<const std::string_view> enclosing_bindings;
  // Filled by the class emitter for its generated inner class expression.
  std::string_view class_binding;
  // Named function expressions bind their own name inside the body.
  std::string_view self_binding{};
  // Source formal names, sorted by byte name for O(log P) collision checks.
  std::span<const std::string_view> parameter_bindings{};
  const HostRealmBinding* realm_binding = nullptr;
  // Record-owned plan, borrowed through synchronous descendant emission.
  // Empty slot names retain the generated $importN spelling.
  std::span<const std::string> import_bindings{};
  std::span<const std::string_view> import_names{};
  const HostSymbolBinding* symbol_binding = nullptr;
  const HostArgumentSnapshotBinding* argument_snapshot_binding = nullptr;
  // Additional explicit output premise for historical contracts. V3 itself
  // permits recovered text; this is never an ABC-derived behavioral proof.
  CallableTextPolicy callable_text = CallableTextPolicy::kUnspecified;
  // Exact native ESM prelude for this destination. Borrowed names alone cannot
  // certify module owner/slot correspondence. Rechecked at tree publication.
  const ModuleEnclosure* module_enclosure = nullptr;
  // Record adapter proves these native let declarations from actual first
  // module stores. Indexed after the enclosure imports; reads remain live/TDZ.
  // No declaration premise is supplied for detached class/function output.
  std::span<const std::u16string> module_local_names{};
  // Empty retains the historical metadata premise; otherwise exactly one
  // evidence entry per declaration, including checked anonymous wire bindings.
  std::span<const NameEvidence> module_local_evidence{};
  // Native record lexical layer, separate from intervening wrappers. Local
  // module aliases refer into this layer; free globals must still avoid it.
  std::span<const std::string_view> record_bindings{};
};

struct RealmBindingView {
  RealmBindingPolicy policy;
  std::u16string_view name;
};
// Syntax/contract selection only. Callers must separately check actual lexical
// ancestry and the complete destination; this does not prove non-shadowing.
[[nodiscard]] std::optional<RealmBindingView> ResolveRealmBinding(
    SourceContract contract, const GlobalScope* scope);
[[nodiscard]] std::optional<std::u16string_view> ResolveSymbolBinding(const GlobalScope* scope);
[[nodiscard]] std::optional<std::u16string_view> ResolveArgumentSnapshotBinding(const GlobalScope* scope);

[[nodiscard]] std::string_view ImportBinding(const GlobalScope* scope,
                                             std::size_t slot);
[[nodiscard]] bool ShadowsImport(std::string_view name, const GlobalScope& scope);

// A free identifier reaches the v7 global record/object only if the generated
// module/class wrapper cannot shadow it. Non-ASCII and strict-reserved names
// stay unsupported until source identifier handling proves their equivalence.
[[nodiscard]] bool AllowsGlobalReference(std::u16string_view name,
                                         GlobalScope scope);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_GLOBAL_REFERENCE_H_
