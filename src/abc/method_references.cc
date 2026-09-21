#include "abcd/abc/method_references.h"

#include <algorithm>
#include <new>
#include <tuple>

namespace abcd::abc {
namespace {
using Role = std::variant<std::monostate, PoolEntityKind, ReferenceError>;
Role PoolRole(OperandKind kind) {
  switch (kind) {
    case OperandKind::kRegister:
    case OperandKind::kImmediate:
      return std::monostate{};
    case OperandKind::kMethod:
      return PoolEntityKind::kMethod;
    case OperandKind::kString:
      return PoolEntityKind::kString;
    case OperandKind::kLiteralArray:
      return PoolEntityKind::kLiteralArray;
    case OperandKind::kType:
    case OperandKind::kField:
      return ReferenceError::kUnsupportedOperand;
  }
  return ReferenceError::kUnsupportedOperand;
}
}  // namespace

ReferenceResult ResolveMethodReferences(const corpus::AbcFile& file,
                                        const Header& header,
                                        const MetadataIndex& index,
                                        EntityOffset method,
                                        const MethodBody& body,
                                        ReferenceBudget budget) {
  const auto fail = [](ReferenceError kind, CodeOffset pc) {
    return ReferenceFailure{kind, pc, std::nullopt};
  };
  if (file.Size() != header.file_size || file.CheckUnchanged())
    return ReferenceFailure{
        ReferenceError::kResolution, {}, MetadataError::kFileChanged};
  std::size_t count = 0;
  for (const auto& instruction : body.instructions) {
    if (instruction.decoded.operand_count > instruction.decoded.operands.size())
      return fail(ReferenceError::kMalformedOperands, instruction.offset);
    for (const auto& operand : std::span(instruction.decoded.operands)
                                   .first(instruction.decoded.operand_count)) {
      const auto role = PoolRole(operand.kind);
      if (const auto* error = std::get_if<ReferenceError>(&role))
        return fail(*error, instruction.offset);
      if (!std::holds_alternative<PoolEntityKind>(role)) continue;
      if (operand.raw_bits > UINT32_MAX)
        return fail(ReferenceError::kIndexOverflow, instruction.offset);
      if (count >= budget.bytes / sizeof(MethodReference))
        return fail(ReferenceError::kResourceLimit, instruction.offset);
      ++count;
    }
  }
  try {
    MethodReferences result;
    result.operand_count = count;
    auto& references = result.references;
    references.reserve(count);
    if (references.capacity() > budget.bytes / sizeof(MethodReference))
      return fail(ReferenceError::kResourceLimit, {});
    for (const auto& instruction : body.instructions) {
      for (const auto& operand :
           std::span(instruction.decoded.operands)
               .first(instruction.decoded.operand_count)) {
        const auto role = PoolRole(operand.kind);
        if (const auto* kind = std::get_if<PoolEntityKind>(&role))
          references.push_back({{static_cast<std::uint32_t>(operand.raw_bits)},
                                {*kind, {}},
                                instruction.offset});
      }
    }
    std::sort(
        references.begin(), references.end(), [](const auto& a, const auto& b) {
          return std::tie(a.target.kind, a.slot.value, a.first_use.value) <
                 std::tie(b.target.kind, b.slot.value, b.first_use.value);
        });
    references.erase(std::unique(references.begin(), references.end(),
                                 [](const auto& a, const auto& b) {
                                   return a.target.kind == b.target.kind &&
                                          a.slot == b.slot;
                                 }),
                     references.end());
    for (auto& reference : references) {
      const auto resolved = ResolvePoolReference(
          file, header, index, method, reference.slot, reference.target.kind);
      if (const auto* error = std::get_if<MetadataError>(&resolved))
        return ReferenceFailure{ReferenceError::kResolution,
                                reference.first_use, *error};
      reference.target = std::get<PoolReference>(resolved);
    }
    if (file.CheckUnchanged())
      return ReferenceFailure{
          ReferenceError::kResolution, {}, MetadataError::kFileChanged};
    return result;
  } catch (const std::bad_alloc&) {
    return fail(ReferenceError::kResourceLimit, {});
  }
}

}  // namespace abcd::abc
