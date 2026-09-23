#include "abcd/source/module_exports.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <streambuf>

#include "abcd/source/string_literal.h"
#include "module_binding_name.h"

namespace abcd::source {
namespace {
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
  int_type overflow(int_type value) override {
    return traits_type::not_eof(value);
  }
};
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
    if (count > budget.bytes) return ModuleExportError::kResourceLimit;
    result.initialized_.resize(count);
    if (result.initialized_.capacity() > budget.bytes)
      return ModuleExportError::kResourceLimit;
    budget.bytes -= result.initialized_.capacity();
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

bool ModuleExports::Complete() const {
  return std::all_of(initialized_.begin(),
                     initialized_.begin() + slot_count_,
                     [](std::uint8_t state) { return state != 0; });
}

std::variant<std::size_t, ModuleExportError> ModuleExports::WriteAliases(
    std::ostream& output, EmitBudget budget) const {
  DiscardBuffer null;
  std::ostream dry(&null);
  const auto checked = WriteAliasesPass(dry, budget);
  if (!std::holds_alternative<std::size_t>(checked))
    return std::get<ModuleExportError>(checked);
  return WriteAliasesPass(output, budget);
}

std::variant<std::size_t, ModuleExportError> ModuleExports::WriteAliasesPass(
    std::ostream& output, EmitBudget budget) const {
  std::size_t used = 0;
  const auto text = [&](std::string_view value) -> bool {
    if (value.size() > budget.bytes - used) return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    used += value.size();
    return static_cast<bool>(output);
  };
  try {
    for (const auto& alias : aliases_) {
      if (!text("export { ") || !text(kLocalModulePrefix))
        return output ? ModuleExportError::kOutputLimit
                      : ModuleExportError::kWrite;
      std::array<char, std::numeric_limits<std::uint16_t>::digits10 + 2>
          digits{};
      const auto encoded = std::to_chars(digits.data(), digits.data() +
                                        digits.size(), alias.slot.value);
      if (encoded.ec != std::errc{}) return ModuleExportError::kResourceLimit;
      if (!text({digits.data(),
                 static_cast<std::size_t>(encoded.ptr - digits.data())}) ||
          !text(" as "))
        return output ? ModuleExportError::kOutputLimit
                      : ModuleExportError::kWrite;
      const auto literal = WriteStringLiteral(alias.exported, output,
                                              {budget.bytes - used});
      if (const auto* error = std::get_if<EmitError>(&literal))
        return *error == EmitError::kOutputLimit
                   ? ModuleExportError::kOutputLimit
                   : ModuleExportError::kWrite;
      used += std::get<std::size_t>(literal);
      if (!text(" };\n"))
        return output ? ModuleExportError::kOutputLimit
                      : ModuleExportError::kWrite;
    }
    return used;
  } catch (const std::ios_base::failure&) {
    return ModuleExportError::kWrite;
  }
}

std::variant<std::size_t, ModuleExportError> ModuleExports::WriteStore(
    analysis::ModuleStore store, std::ostream& output, EmitBudget budget) {
  if (store.slot.value >= slot_count_) return ModuleExportError::kSlot;
  std::array<char, std::numeric_limits<std::uint16_t>::digits10 + 2> digits{};
  const auto encoded = std::to_chars(digits.data(), digits.data() + digits.size(),
                                     store.slot.value);
  if (encoded.ec != std::errc{}) return ModuleExportError::kResourceLimit;
  const std::string_view ordinal{
      digits.data(), static_cast<std::size_t>(encoded.ptr - digits.data())};
  constexpr std::string_view kName = kLocalModulePrefix;
  constexpr std::string_view kValue = " = $acc;\n";
  const bool first = !initialized_[store.slot.value];
  constexpr std::string_view kDeclaration = "let ";
  const auto bytes = (first ? kDeclaration.size() : 0) + kName.size() +
                     ordinal.size() + kValue.size();
  if (bytes > budget.bytes) return ModuleExportError::kOutputLimit;
  try {
    if (first) output << kDeclaration;
    output << kName << ordinal << kValue;
    if (!output) return ModuleExportError::kWrite;
    initialized_[store.slot.value] = 1;
    return bytes;
  } catch (const std::ios_base::failure&) {
    return ModuleExportError::kWrite;
  }
}
}  // namespace abcd::source
