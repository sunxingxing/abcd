#ifndef ABCD_SOURCE_OBJECT_CONSTANTS_H_
#define ABCD_SOURCE_OBJECT_CONSTANTS_H_

#include "tree_constants.h"

namespace abcd::source {
struct ObjectConstantStorage { std::size_t bytes; std::uint64_t work; };
// Appends typed primitive object properties and dense array elements to the same constant arenas.
// Existing constant storage is caller-admitted; failure discards the complete
// partially constructed TreeConstants. Never calls a legacy source writer.
[[nodiscard]] std::variant<ObjectConstantStorage, TreeConstantError, abc::MetadataError>
ResolveObjectConstants(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, abc::EntityOffset method,
    const analysis::ValueProgram& graph, TreeConstants& constants,
    analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif
