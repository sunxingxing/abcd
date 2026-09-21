#ifndef ABCD_ABC_LITERAL_ARRAY_H_
#define ABCD_ABC_LITERAL_ARRAY_H_

#include "abcd/abc/metadata_index.h"

namespace abcd::abc {

// Pinned runtime_core libpandafile/literal_data_accessor.h. The numeric values
// are format tags, not permission to use every tag in every dynamic opcode.
enum class LiteralTag : std::uint8_t {
  kBoolean = 0x01,
  kInteger = 0x02,
  kFloat = 0x03,
  kDouble = 0x04,
  kString = 0x05,
  kMethod = 0x06,
  kGeneratorMethod = 0x07,
  kAccessor = 0x08,
  kMethodAffiliate = 0x09,
  kAsyncGeneratorMethod = 0x16,
  kLiteralBufferIndex = 0x17,
  kLiteralArray = 0x18,
  kBuiltinTypeIndex = 0x19,
  kGetter = 0x1a,
  kSetter = 0x1b,
  kImplements = 0x1c,
  kNull = 0xff,
};

struct LiteralBits {
  std::uint64_t value = 0;
};
struct LiteralEntry {
  LiteralTag tag;
  std::variant<LiteralBits, EntityOffset> value;
};
struct LiteralArray {
  std::vector<LiteralEntry> entries;
  EntityOffset end;
};
enum class LiteralError {
  kUnsupportedProfile,
  kFileChanged,
  kRead,
  kTruncated,
  kReference,
  kOddCount,
  kUnknownTag,
  kUnsupportedTag,
  kPackedArray,
  kResourceLimit,
};
struct LiteralFailure {
  LiteralError kind;
  EntityOffset offset;
};
struct LiteralBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};
using LiteralResult = std::variant<LiteralArray, LiteralFailure>;

// A direct entity offset, not a global literal index or regional pool slot.
// Preserve tag order and raw numeric bits; do not eagerly follow references.
// O(entries) time/storage. Budget covers output capacity; caller owns
// admission.
[[nodiscard]] LiteralResult ReadLiteralArray(const corpus::AbcFile& file,
                                             const Header& header,
                                             EntityOffset offset,
                                             LiteralBudget budget = {});

}  // namespace abcd::abc
#endif  // ABCD_ABC_LITERAL_ARRAY_H_
