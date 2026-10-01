#ifndef ABCD_SOURCE_STAGED_LITERAL_NAMES_H_
#define ABCD_SOURCE_STAGED_LITERAL_NAMES_H_

#include <algorithm>
#include <string_view>
#include "expression_constants.h"
#include "literal_plan.h"

namespace abcd::source {
// Constants certify existing writable own data properties. Combined with the
// plan's no-escape interval, a staged named assignment has the same effect as
// the original own definition, without observing an inherited setter. Compare
// spellings through a sorted buffer index, never scan every key for each write.
template<class Step, class Admit>
bool CheckStagedLiteralNames(const LiteralPlan& plan, ExpressionConstants constants,
    std::span<const std::u16string> names, Step step, Admit admit) {
  for (const auto& object : plan.objects) {
    step(1); if (object.form != LiteralForm::kStatements) continue;
    if (object.allocation.value >= constants.operations.size()) return false;
    const auto* buffer = std::get_if<AggregateConstant>(&constants.operations[object.allocation.value]);
    if (!buffer || buffer->kind != AggregateKind::kObject || buffer->first > constants.properties.size() ||
        buffer->count > constants.properties.size() - buffer->first) return false;
    std::vector<std::u16string_view> keys;
    admit(buffer->count, sizeof(std::u16string_view)); keys.reserve(buffer->count);
    admit(keys.capacity() - buffer->count, sizeof(std::u16string_view));
    for (std::uint32_t i = 0; i < buffer->count; ++i) {
      step(1); const auto& property = constants.properties[buffer->first + i];
      if (property.key.value >= constants.name_count || property.key.value >= names.size() || std::holds_alternative<UndefinedLiteral>(property.value)) return false;
      keys.push_back(names[property.key.value]);
    }
    const auto less = [&](std::u16string_view a, std::u16string_view b) {
      step(std::min(a.size(), b.size()) + 1); return a < b;
    };
    std::sort(keys.begin(), keys.end(), less);
    for (std::uint32_t i = 0; i < object.count; ++i) {
      step(1); const auto id = plan.definitions[object.first + i];
      if (id.value >= constants.operations.size()) return false;
      const auto* name = std::get_if<NameId>(&constants.operations[id.value]);
      if (!name || name->value >= constants.name_count || name->value >= names.size() || !std::binary_search(keys.begin(), keys.end(), names[name->value], less)) return false;
    }
  }
  return true;
}
}  // namespace abcd::source
#endif
