#ifndef ABCD_SOURCE_RECORD_BUFFER_H_
#define ABCD_SOURCE_RECORD_BUFFER_H_

#include <variant>

#include "abcd/abc/literal_array.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/string_entity.h"
#include "record_literal.h"

namespace abcd::source {
struct BufferedRecordObject {
  std::vector<RecordLiteralField> fields;
  std::size_t retained_bytes = 0;
};
enum class RecordBufferError {
  kShape,
  kKey,
  kValue,
  kDuplicateKey,
  kResourceLimit
};
using RecordBufferResult =
    std::variant<BufferedRecordObject, RecordBufferError,
                 abc::MetadataError, abc::LiteralFailure>;

[[nodiscard]] RecordBufferResult ReadRecordObjectBuffer(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, abc::EntityOffset method,
    abc::PoolIndex literal, std::size_t bytes, abc::StringBudget strings);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_BUFFER_H_
