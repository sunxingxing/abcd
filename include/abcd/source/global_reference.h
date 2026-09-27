#ifndef ABCD_SOURCE_GLOBAL_REFERENCE_H_
#define ABCD_SOURCE_GLOBAL_REFERENCE_H_

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include "abcd/source/source_contract.h"

namespace abcd::source {
class ModuleExports;
// Embedding-host trust boundary, not evidence inferred from a name or ABC.
// Host supplies an initialized immutable lexical binding to this ABC realm's
// original global object, visible for the generated code's entire lifetime.
// The borrowed name must remain stable during synchronous source emission.
struct HostRealmBinding {
  std::u16string_view name;
};
struct GlobalScope {
  // Caller-proven complete ASCII outer source bindings, sorted by byte name;
  // non-ASCII names cannot collide with admitted ASCII global references.
  // Build once per record, never rescan per method.
  std::span<const std::string_view> enclosing_bindings;
  // Filled by the class emitter for its generated inner class expression.
  std::string_view class_binding;
  // Named function expressions bind their own name inside the body.
  std::string_view self_binding{};
  // Source formal names, sorted by byte name for O(log P) collision checks.
  std::span<const std::string_view> parameter_bindings{};
  // Caller-proven same-record $local slots that will be declared by
  // STMODULEVAR. Zero forbids local module reads in detached method source.
  std::size_t module_slots = 0;
  // Record emission is synchronous. This points to the store state at the
  // creation site; no later STMODULEVAR is processed until nested source ends.
  const ModuleExports* module_exports = nullptr;
  const HostRealmBinding* realm_binding = nullptr;
  // Record-owned plan, borrowed through synchronous descendant emission.
  // Empty slot names retain the generated $importN spelling.
  std::span<const std::string> import_bindings{};
  std::span<const std::string_view> import_names{};
};

struct RealmBindingView {
  RealmBindingPolicy policy;
  std::u16string_view name;
};
// Syntax/contract selection only. Callers must separately check actual lexical
// ancestry and the complete destination; this does not prove non-shadowing.
[[nodiscard]] std::optional<RealmBindingView> ResolveRealmBinding(
    SourceContract contract, const GlobalScope* scope);

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
