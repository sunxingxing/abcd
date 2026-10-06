#ifndef ABCD_SOURCE_SYNTAX_RESOLUTION_H_
#define ABCD_SOURCE_SYNTAX_RESOLUTION_H_

#include "function_context.h"
#include "function_syntax.h"
#include "tree_constants.h"

namespace abcd::source {
struct ResolvedSyntax {
  FunctionSyntax syntax;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  // Name/text payloads reside in syntax; operation IDs still index that prefix.
  std::vector<ExpressionConstant> operations;
  std::optional<TextId> number_type_text;
  std::vector<ConstantProperty> properties;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Take this view before moving syntax to the tree builder; retain operation
  // and property arenas until that synchronous construction/checking finishes.
  [[nodiscard]] ExpressionConstants View() const {
    return {operations, syntax.names.size(), syntax.texts.size(),
        number_type_text, properties};
  }
};
enum class SyntaxResolutionError : std::uint8_t {
  kContext, kParameters, kResourceLimit, kWorkLimit
};
using SyntaxResolutionFailure = std::variant<SyntaxResolutionError,
    TreeConstantError, abc::MetadataError>;
// Shared by standalone and family preparation. Context, body and verified
// graph belong to the same unchanged file/method; the context supplies actual
// callable/strictness/frame facts. Owner identifies this syntax occurrence,
// not a method-table index. Defaults, incoming captures/modules and enclosing
// globals are separate proof steps, never inferred from successful naming.
// Existing inputs are caller-admitted. Constants and recovered-name storage,
// including replaced arena capacity, share this additional byte allowance.
[[nodiscard]] std::variant<ResolvedSyntax, SyntaxResolutionFailure> ResolveFunctionSyntax(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const FunctionContext& context,
    const abc::MethodBody& body, const analysis::ValueProgram& graph,
    FunctionOwnerId owner, analysis::SemanticBudget budget = {},
    std::span<const SignatureString> signature = {}, const CellPlan* cells = nullptr);
}  // namespace abcd::source
#endif
