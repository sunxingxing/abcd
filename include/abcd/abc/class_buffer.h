#ifndef ABCD_ABC_CLASS_BUFFER_H_
#define ABCD_ABC_CLASS_BUFFER_H_

#include "abcd/abc/literal_array.h"
#include "abcd/abc/class_kind.h"

namespace abcd::abc {
enum class ClassPlacement { kPrototype, kStatic };
enum class ClassMethodKind {
  kOrdinary,
  kGetter,
  kSetter,
  kGenerator,
  kAsyncGenerator
};
struct ClassMethod {
  EntityOffset target;
  ClassMethodKind kind;
  std::uint16_t length = 0;
};
struct ClassProperty {
  ClassPlacement placement;
  LiteralEntry key;
  std::variant<LiteralEntry, ClassMethod> value;
};
struct ClassBuffer {
  std::vector<ClassProperty> properties;
  ClassKind kind = ClassKind::kOrdinary;
  std::optional<EntityOffset> field_types;
};
enum class ClassBufferError {
  kShape,
  kKey,
  kValue,
  kAffiliate,
  kSplit,
  kResourceLimit
};
struct ClassBufferBudget {
  std::size_t bytes = 16 * 1024 * 1024;
};
// Ordinary class buffer only. Input owns validated literal payloads. Output
// preserves definition order/duplicates; prototype properties are NOT instance
// fields. Target kind is template evidence, not overriding method attributes.
[[nodiscard]] std::variant<ClassBuffer, ClassBufferError> DecodeClassBuffer(
    const LiteralArray& literals, ClassBufferBudget budget = {});
// The caller proves kind from the original creation. Sendable's final
// LITERALARRAY is a direct entity reference, not a pool slot or member.
[[nodiscard]] std::variant<ClassBuffer, ClassBufferError> DecodeClassLiteral(
    const LiteralArray&, ClassKind, ClassBufferBudget = {});
}  // namespace abcd::abc
#endif
