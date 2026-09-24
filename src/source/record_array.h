#ifndef ABCD_SOURCE_RECORD_ARRAY_H_
#define ABCD_SOURCE_RECORD_ARRAY_H_

#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/literal_array.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/literal_graph.h"
#include "abcd/analysis/array_creation.h"

namespace abcd::source {
struct BufferedArray {
  std::vector<StaticAtom> elements;
  std::size_t retained_bytes = 0;
};
enum class BufferedArrayError { kInstruction, kValue, kResourceLimit };
using BufferedArrayResult =
    std::variant<BufferedArray, BufferedArrayError, abc::MetadataError,
                 abc::LiteralFailure>;

// Reads only primitive dense buffers. The caller owns the returned storage;
// decoding scratch and retained elements share one byte budget.
[[nodiscard]] BufferedArrayResult ReadBufferedArray(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, abc::EntityOffset method,
    const analysis::BufferedArrayCreation& creation, std::size_t bytes,
    abc::StringBudget strings);
[[nodiscard]] BufferedArrayResult ReadBufferedArray(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, abc::EntityOffset method,
    const abc::DecodedInstruction& instruction, std::size_t bytes,
    abc::StringBudget strings);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_ARRAY_H_
