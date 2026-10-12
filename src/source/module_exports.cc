#include "abcd/source/module_exports.h"

#include <algorithm>
#include <new>
#include <numeric>
#include <stdexcept>


namespace abcd::source {
namespace {
bool Supported(abc::ProfileId profile) {
  switch (profile) {
    case abc::ProfileId::kDynamic9:
    case abc::ProfileId::kDynamic1102:
    case abc::ProfileId::kDynamic1202:
    case abc::ProfileId::kDynamic1206:
    case abc::ProfileId::kDynamic1301:
    case abc::ProfileId::kDynamic2400:
      return true;
    case abc::ProfileId::kUnknown:
    case abc::ProfileId::kStatic0107:
      return false;
  }
  return false;
}
}  // namespace

ModuleExportResult BuildModuleExports(const corpus::AbcFile& file,
                                      const abc::Header& header,
                                      const abc::ModuleInfo& module,
                                      ModuleExportBudget budget) {
  if (header.kind != abc::AbcKind::kDynamic || !Supported(header.profile))
    return ModuleExportError::kProfile;
  try {
    const auto count = module.local_exports.size();
    if (count > budget.bytes / sizeof(ModuleExports::Alias))
      return ModuleExportError::kResourceLimit;
    ModuleExports result;
    result.aliases_.reserve(count);
    const auto alias_bytes = result.aliases_.capacity() *
                             sizeof(ModuleExports::Alias);
    if (alias_bytes > budget.bytes) return ModuleExportError::kResourceLimit;
    budget.bytes -= alias_bytes;
    if (count > budget.bytes / sizeof(std::size_t))
      return ModuleExportError::kResourceLimit;
    std::vector<std::size_t> order(count);
    if (order.capacity() > budget.bytes / sizeof(std::size_t))
      return ModuleExportError::kResourceLimit;
    budget.bytes -= order.capacity() * sizeof(std::size_t);
    std::u16string previous;
    for (const auto& entry : module.local_exports) {
      auto local = abc::ReadStringEntity(file, header, entry.local,
                                         budget.strings);
      if (const auto* error = std::get_if<abc::MetadataError>(&local))
        return *error;
      auto& name = std::get<std::u16string>(local);
      if ((name.capacity() + previous.capacity()) >
          budget.bytes / sizeof(char16_t))
        return ModuleExportError::kResourceLimit;
      if (result.aliases_.empty() || name != previous) {
        if (result.slot_count_ > UINT16_MAX) return ModuleExportError::kSlot;
        ++result.slot_count_;
      }
      const auto slot = static_cast<std::uint16_t>(result.slot_count_ - 1);
      previous = std::move(name);
      auto exported = abc::ReadStringEntity(file, header, entry.exported,
                                            budget.strings);
      if (const auto* error = std::get_if<abc::MetadataError>(&exported))
        return *error;
      auto& alias = std::get<std::u16string>(exported);
      if (previous.capacity() + alias.capacity() >
          budget.bytes / sizeof(char16_t))
        return ModuleExportError::kResourceLimit;
      result.aliases_.push_back({{slot}, std::move(alias)});
      const auto retained = result.aliases_.back().exported.capacity() *
                            sizeof(char16_t);
      if (retained > budget.bytes)
        return ModuleExportError::kResourceLimit;
      budget.bytes -= retained;
    }
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return result.aliases_[a].exported < result.aliases_[b].exported;
    });
    for (std::size_t i = 1; i < order.size(); ++i)
      if (result.aliases_[order[i - 1]].exported ==
          result.aliases_[order[i]].exported)
        return ModuleExportError::kDuplicateExport;
    return result;
  } catch (const std::bad_alloc&) {
    return ModuleExportError::kResourceLimit;
  } catch (const std::length_error&) {
    return ModuleExportError::kResourceLimit;
  }
}

}  // namespace abcd::source
