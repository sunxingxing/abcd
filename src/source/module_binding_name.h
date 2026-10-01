#ifndef ABCD_SOURCE_MODULE_BINDING_NAME_H_
#define ABCD_SOURCE_MODULE_BINDING_NAME_H_

#include <string_view>

#include "abcd/abc/module_info.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
inline constexpr std::string_view kLocalModulePrefix = "$local";
// v7 es2panda/parser/module/sourceTextModuleRecord.h DEFAULT_LOCAL_NAME.
// This is a validated wire boundary, never an emitted identifier or semantic tag.
inline constexpr std::u16string_view kAnonymousDefaultLocal = u"*default*";
struct ModuleBindingNames {
  std::vector<std::u16string> locals; // Original adjacent-name VM slot order.
  std::vector<NameEvidence> evidence;
  std::vector<std::size_t> rows; // local-export row -> local VM slot.
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ModuleBindingNameError : std::uint8_t { kName, kResourceLimit, kWorkLimit };
using ModuleBindingNameFailure = std::variant<ModuleBindingNameError, abc::MetadataError>;
// Derive the same slot spelling for record stores and nested reads from the
// complete metadata inventory. Imported/user names are never renamed. Guards
// still compare original wire names before this adapter is consulted.
[[nodiscard]] std::variant<ModuleBindingNames, ModuleBindingNameFailure> ResolveModuleBindingNames(
    const corpus::AbcFile& file, const abc::Header& header, const abc::ModuleInfo& module,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
