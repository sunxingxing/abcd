#include "abcd/abc/class_buffer.h"

#include <limits>
#include <stdexcept>

namespace abcd::abc {
namespace {
std::optional<ClassMethodKind> MethodKind(LiteralTag tag) {
  switch (tag) {
    case LiteralTag::kMethod:
      return ClassMethodKind::kOrdinary;
    case LiteralTag::kGetter:
      return ClassMethodKind::kGetter;
    case LiteralTag::kSetter:
      return ClassMethodKind::kSetter;
    case LiteralTag::kGeneratorMethod:
      return ClassMethodKind::kGenerator;
    case LiteralTag::kAsyncGeneratorMethod:
      return ClassMethodKind::kAsyncGenerator;
    default:
      return std::nullopt;
  }
}
bool Scalar(const LiteralEntry& entry) {
  if (entry.tag == LiteralTag::kString)
    return std::holds_alternative<EntityOffset>(entry.value);
  const auto* bits = std::get_if<LiteralBits>(&entry.value);
  if (!bits) return false;
  switch (entry.tag) {
    case LiteralTag::kBoolean:
      return bits->value <= 1;
    case LiteralTag::kInteger:
      return bits->value <= UINT32_MAX;
    case LiteralTag::kDouble:
    case LiteralTag::kAccessor:
    case LiteralTag::kNull:
      return true;
    default:
      return false;
  }
}
}  // namespace
std::variant<ClassBuffer, ClassBufferError> DecodeClassBuffer(
    const LiteralArray& literals, ClassBufferBudget budget) {
  return DecodeClassLiteral(literals, ClassKind::kOrdinary, budget);
}
std::variant<ClassBuffer, ClassBufferError> DecodeClassLiteral(
    const LiteralArray& literals, ClassKind kind, ClassBufferBudget budget) {
  if (kind != ClassKind::kOrdinary && kind != ClassKind::kSendable) return ClassBufferError::kShape;
  auto entries = std::span<const LiteralEntry>(literals.entries);
  std::optional<EntityOffset> field_types;
  switch (kind) {
    case ClassKind::kOrdinary: break;
    case ClassKind::kSendable: {
      if (entries.empty() || entries.back().tag != LiteralTag::kLiteralArray)
        return ClassBufferError::kShape;
      const auto* target = std::get_if<EntityOffset>(&entries.back().value);
      if (!target || target->value < kDynamicHeaderSize)
        return ClassBufferError::kValue;
      field_types = *target;
      entries = entries.first(entries.size() - 1);
      break;
    }
  }
  if (entries.empty() || entries.back().tag != LiteralTag::kInteger)
    return ClassBufferError::kShape;
  const auto* split = std::get_if<LiteralBits>(&entries.back().value);
  if (!split || split->value > UINT32_MAX) return ClassBufferError::kSplit;
  try {
    ClassBuffer result;
    result.kind = kind;
    result.field_types = field_types;
    // Each member has at least key/value; method+affiliate consumes one extra
    // entry. Upper-bound reserve avoids reallocations and quadratic growth.
    constexpr std::size_t kPairEntries = 2;
    const auto capacity = (entries.size() - 1) / kPairEntries;
    if (capacity > budget.bytes / sizeof(ClassProperty))
      return ClassBufferError::kResourceLimit;
    result.properties.reserve(capacity);
    if (result.properties.capacity() > budget.bytes / sizeof(ClassProperty))
      return ClassBufferError::kResourceLimit;
    const auto end = entries.size() - 1;
    for (std::size_t i = 0; i < end;) {
      const auto& key = entries[i++];
      if (!Scalar(key) ||
          (key.tag != LiteralTag::kString && key.tag != LiteralTag::kInteger &&
           key.tag != LiteralTag::kDouble))
        return ClassBufferError::kKey;
      if (i == end) return ClassBufferError::kShape;
      const auto& value = entries[i++];
      ClassProperty property{result.properties.size() < split->value
                                 ? ClassPlacement::kPrototype
                                 : ClassPlacement::kStatic,
                             key, value};
      if (const auto kind = MethodKind(value.tag)) {
        const auto* target = std::get_if<EntityOffset>(&value.value);
        if (!target) return ClassBufferError::kValue;
        if (i == end || entries[i].tag != LiteralTag::kMethodAffiliate)
          return ClassBufferError::kAffiliate;
        const auto* length = std::get_if<LiteralBits>(&entries[i++].value);
        if (!length || length->value > UINT16_MAX)
          return ClassBufferError::kAffiliate;
        property.value = ClassMethod{*target, *kind,
                                     static_cast<std::uint16_t>(length->value)};
      } else if (!Scalar(value))
        return ClassBufferError::kValue;
      result.properties.push_back(property);
    }
    if (split->value > result.properties.size())
      return ClassBufferError::kSplit;
    return result;
  } catch (const std::bad_alloc&) {
    return ClassBufferError::kResourceLimit;
  } catch (const std::length_error&) {
    return ClassBufferError::kResourceLimit;
  }
}
}  // namespace abcd::abc
