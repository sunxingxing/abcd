#ifndef ABCD_SOURCE_CELL_NAMES_H_
#define ABCD_SOURCE_CELL_NAMES_H_
#include "function_syntax.h"
#include "tree_constants.h"
namespace abcd::source {
struct CellNameStorage { std::size_t bytes; std::uint64_t work; };
// Presentation hints only. Group identical scope literals, validate slot pairs,
// and omit ambiguous/invalid identifiers; never infer storage from a spelling.
[[nodiscard]] std::variant<CellNameStorage, TreeConstantError, abc::MetadataError> ResolveCellNames(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&, abc::EntityOffset,
    const analysis::ValueProgram&, std::vector<std::u16string>&, std::vector<LocalNameSyntax>&,
    analysis::SemanticBudget = {});
}
#endif
