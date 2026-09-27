#ifndef ABCD_SOURCE_TREE_CONSTANTS_H_
#define ABCD_SOURCE_TREE_CONSTANTS_H_

#include "abcd/abc/string_entity.h"
#include "expression_constants.h"

namespace abcd::source {
struct TreeConstants {
  abc::EntityOffset method;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::vector<std::u16string> names;
  std::vector<std::u16string> texts;
  std::vector<ExpressionConstant> operations;
  std::optional<TextId> number_type_text;
  std::vector<ConstantProperty> properties;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] ExpressionConstants View() const {
    return {operations, names.size(), texts.size(), number_type_text, properties};
  }
};
enum class TreeConstantError : std::uint8_t {
  kElementName, kResourceLimit, kWorkLimit, kObjectLiteral
};
using TreeConstantResult = std::variant<TreeConstants, TreeConstantError, abc::MetadataError>;
// Same-file metadata/graph precondition. Sort/deduplicate pool slots, then
// resolved entities: aliasing slots do not reread/copy the same string once per
// instruction. Ordinary named access keeps the existing element-key refusal.
[[nodiscard]] TreeConstantResult ResolveTreeConstants(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, abc::EntityOffset method,
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
