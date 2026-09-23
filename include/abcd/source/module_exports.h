#ifndef ABCD_SOURCE_MODULE_EXPORTS_H_
#define ABCD_SOURCE_MODULE_EXPORTS_H_

#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

#include "abcd/abc/module_info.h"
#include "abcd/abc/string_entity.h"
#include "abcd/analysis/module_store.h"
#include "abcd/source/emission.h"

namespace abcd::source {
enum class ModuleExportError {
  kProfile,
  kResourceLimit,
  kDuplicateExport,
  kSlot,
  kOutputLimit,
  kWrite
};
struct ModuleExportBudget {
  std::size_t bytes = 16 * 1024 * 1024;
  abc::StringBudget strings;
};
class ModuleExports {
 public:
  [[nodiscard]] std::size_t slot_count() const { return slot_count_; }
  [[nodiscard]] bool Complete() const;
  [[nodiscard]] std::variant<std::size_t, ModuleExportError> WriteAliases(
      std::ostream& output, EmitBudget budget = {}) const;
  // Only a caller-proven top-level, straight-line STMODULEVAR may use this.
  // The first write declares a lexical export slot at its original point;
  // subsequent writes update the same live binding. The caller proves $acc is
  // a language value and owns atomic publication of the complete record.
  [[nodiscard]] std::variant<std::size_t, ModuleExportError> WriteStore(
      analysis::ModuleStore store, std::ostream& output,
      EmitBudget budget = {});

 private:
  friend std::variant<ModuleExports, ModuleExportError, abc::MetadataError>
  BuildModuleExports(const corpus::AbcFile&, const abc::Header&,
                     const abc::ModuleInfo&, ModuleExportBudget);
  struct Alias {
    analysis::ModuleSlot slot;
    std::u16string exported;
  };
  [[nodiscard]] std::variant<std::size_t, ModuleExportError>
  WriteAliasesPass(std::ostream& output, EmitBudget budget) const;
  std::vector<Alias> aliases_;
  std::vector<std::uint8_t> initialized_;
  std::size_t slot_count_ = 0;
};
using ModuleExportResult =
    std::variant<ModuleExports, ModuleExportError, abc::MetadataError>;
// All supported dynamic profiles use adjacent-equal local export names to
// share a VM slot. Nonadjacent repeats remain distinct slots, not a name map.
[[nodiscard]] ModuleExportResult BuildModuleExports(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo& module, ModuleExportBudget budget = {});
}  // namespace abcd::source

#endif  // ABCD_SOURCE_MODULE_EXPORTS_H_
