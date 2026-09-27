#ifndef ABCD_SOURCE_MODULE_ENCLOSURE_H_
#define ABCD_SOURCE_MODULE_ENCLOSURE_H_

#include "abcd/source/module_imports.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
struct GlobalScope;
enum class ModuleEnclosureError : std::uint8_t {
  kIdentity, kMissingModule, kName, kDuplicate, kBinding, kShadowed,
  kResourceLimit, kWorkLimit, kFileChanged, kMetadata
};
struct ModuleDestination { std::uint64_t work; };

// Native ESM import inventory, not a promise that imports are initialized.
// Emit this exact prelude in the destination record. Native reads preserve TDZ
// and live bindings; local module slots require a separate declaration proof.
// File lifetime/input fingerprint are caller-owned, as for RecordModuleContext.
class ModuleEnclosure {
 public:
  ModuleEnclosure(ModuleEnclosure&&) noexcept = default;
  ModuleEnclosure& operator=(ModuleEnclosure&&) noexcept = default;
  [[nodiscard]] static std::variant<ModuleEnclosure, ModuleEnclosureError> Build(
      const corpus::AbcFile& file, const abc::Header& header,
      const RecordModuleContext& context, RouteInputIdentity identity,
      ModuleOwnerId owner, analysis::SemanticBudget budget = {});
  const RecordModuleContext& Context() const { return context_; }
  const RouteInputIdentity& Identity() const { return identity_; }
  ModuleOwnerId Owner() const { return owner_; }
  std::span<const std::string> Names() const { return names_; }
  std::span<const std::string_view> SortedNames() const { return sorted_; }
  std::size_t AllocatedBytes() const { return allocated_bytes_; }
  std::uint64_t Work() const { return work_; }
  [[nodiscard]] ModuleImportResult WriteImports(const corpus::AbcFile& file,
      const abc::Header& header, std::ostream& output, EmitBudget budget = {},
      abc::StringBudget strings = {}) const;

 private:
  ModuleEnclosure(RecordModuleContext context, RouteInputIdentity identity,
      ModuleOwnerId owner) : context_(std::move(context)), identity_(identity), owner_(owner) {}
  RecordModuleContext context_;
  RouteInputIdentity identity_;
  ModuleOwnerId owner_;
  std::vector<std::string> names_;
  // Views point into the moved vector buffer. Copying this owner is forbidden.
  std::vector<std::string_view> sorted_;
  std::size_t allocated_bytes_ = 0;
  std::uint64_t work_ = 0;
};
// Recheck actual aliases and complete intervening lexical bindings at every
// publication. Input tree must already have passed shared scope validation.
[[nodiscard]] std::variant<ModuleDestination, ModuleEnclosureError> VerifyModuleDestination(
    const FunctionTree& tree, abc::EntityOffset record, const GlobalScope* scope,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
