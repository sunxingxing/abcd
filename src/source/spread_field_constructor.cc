#include "spread_field_constructor.h"

#include <new>
#include <ostream>
#include <string_view>

#include "abcd/abc/pool_reference.h"
#include "abcd/source/string_literal.h"
#include "capture_stream.h"
#include "element_name.h"

namespace abcd::source {
namespace {
FunctionEmitResult Write(
    const analysis::SpreadFieldDerivedConstructor& proof,
    const MethodConstants& constants, std::ostream& output,
    EmitBudget budget) {
  std::size_t written = 0;
  const auto append = [&](std::string_view text) -> std::optional<EmitError> {
    if (text.size() > budget.bytes - written)
      return EmitError::kOutputLimit;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) return EmitError::kWrite;
    written += text.size();
    return std::nullopt;
  };
  const auto constant = [&](abc::PoolIndex slot, bool property)
      -> std::optional<FunctionEmitResult> {
    const auto reference = abc::ResolvePoolReference(
        constants.file, constants.header, constants.index,
        constants.method, slot, abc::PoolEntityKind::kString);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      return BodyEmitFailure{BodyEmitError::kConstant, {}, {}, {}, *error};
    const auto string = abc::ReadStringEntity(
        constants.file, constants.header,
        std::get<abc::PoolReference>(reference).offset,
        constants.string_budget);
    if (const auto* error = std::get_if<abc::MetadataError>(&string))
      return BodyEmitFailure{BodyEmitError::kConstant, {}, {}, {}, *error};
    const auto& value = std::get<std::u16string>(string);
    if (property && IsElementName(value))
      return BodyEmitFailure{BodyEmitError::kIndexedName, {}, {}, {}};
    const auto result = WriteStringLiteral(
        value, output, {budget.bytes - written});
    if (const auto* error = std::get_if<EmitError>(&result)) return *error;
    written += std::get<std::size_t>(result);
    return std::nullopt;
  };
  if (const auto error = append("constructor(...$rest) { super(...$rest); "))
    return *error;
  for (const auto& field : proof.writes) {
    if (const auto error = append("this[")) return *error;
    if (const auto error = constant(field.name, true)) return *error;
    if (const auto error = append("] = ")) return *error;
    if (const auto error = constant(field.value, false)) return *error;
    if (const auto error = append("; ")) return *error;
  }
  if (const auto error = append("}")) return *error;
  return written;
}
}  // namespace

FunctionEmitResult WriteSpreadFieldConstructor(
    const analysis::SpreadFieldDerivedConstructor& proof,
    std::uint16_t length, const MethodConstants& constants,
    std::ostream& output, EmitBudget budget) {
  if (length != 0) return FunctionEmitError::kLength;
  try {
    CaptureDiscard discard;
    std::ostream dry(&discard);
    const auto checked = Write(proof, constants, dry, budget);
    if (!std::holds_alternative<std::size_t>(checked)) return checked;
    return Write(proof, constants, output, budget);
  } catch (const std::bad_alloc&) {
    return FunctionEmitError::kResourceLimit;
  } catch (const std::ios_base::failure&) {
    return EmitError::kWrite;
  }
}
}  // namespace abcd::source
