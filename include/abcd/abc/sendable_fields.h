#ifndef ABCD_ABC_SENDABLE_FIELDS_H_
#define ABCD_ABC_SENDABLE_FIELDS_H_
#include "abcd/abc/class_buffer.h"
#include <limits>
namespace abcd::abc {
// OH7 classDefinition.h::FieldType / class_info_extractor.h::FieldType.
// This is a runtime storage constraint, not a recovered nominal TS type.
enum class SendableFieldType : std::uint8_t {
  kNumber = 1, kString = 2, kBoolean = 4, kTypeReference = 8,
  kBigInt = 16, kGeneric = 32, kNull = 64, kUndefined = 128
};
enum class SendableFieldPlacement : std::uint8_t { kInstance, kStatic };
struct SendableTypeMask { std::uint8_t bits = 0; };
struct SendableField {
  LiteralEntry key;
  SendableTypeMask type;
  SendableFieldPlacement placement;
};
struct SendableFields {
  std::vector<SendableField> fields;
  std::uint32_t instance_count = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class SendableFieldError : std::uint8_t {
  kShape, kKey, kMask, kSplit, kResourceLimit, kWorkLimit
};
struct SendableFieldFailure {
  SendableFieldError kind;
  std::optional<std::uint32_t> field;
  bool operator==(const SendableFieldFailure&) const = default;
};
struct SendableFieldBudget {
  std::size_t bytes = 16 * 1024 * 1024;
  std::uint64_t work = std::numeric_limits<std::uint64_t>::max();
};
// Original field literal, not the member literal. Order and duplicate names are
// retained; NONE/GENERIC remain representability obligations, not fake types.
[[nodiscard]] std::variant<SendableFields, SendableFieldFailure> DecodeSendableFields(
    const LiteralArray&, SendableFieldBudget = {});
}
#endif
