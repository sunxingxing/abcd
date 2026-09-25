#ifndef ABCD_SOURCE_REGISTER_COMMITS_H_
#define ABCD_SOURCE_REGISTER_COMMITS_H_

#include <type_traits>
#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Potential emitted register commits, including result-owning protocols.
// Expression-only targets are not writes. Keep this visitor exhaustive.
template <typename Visitor>
void VisitRegisterCommits(const SourceAst& ast, Visitor visit) {
  for (const auto& node : ast.statements) {
    std::visit([&](const auto& operation) {
      using T = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<T, SourceAssignment>) {
        const auto* reg = std::get_if<abc::RegisterId>(&operation.target);
        if (reg && operation.result == AssignmentResult::kStored) visit(node, *reg);
      } else if constexpr (std::is_same_v<T, SourceStagedCall>) {
        if (operation.stored_result) visit(node, operation.stored_result->target);
      } else if constexpr (std::is_same_v<T, SourceArrayLiteral> ||
                           std::is_same_v<T, SourceArrayCommit>) {
        visit(node, operation.target);
      } else {
        static_assert(std::is_same_v<T, analysis::NoOperation> ||
                      std::is_same_v<T, SourceMemberStore> ||
                      std::is_same_v<T, SourceOwnStore> ||
                      std::is_same_v<T, SourceLocalStore> ||
                      std::is_same_v<T, SourceLocalAssignment> ||
                      std::is_same_v<T, SourceModuleStore> ||
                      std::is_same_v<T, SourceMethodStore> ||
                      std::is_same_v<T, SourceIndexedStore> ||
                      std::is_same_v<T, SourceArrayCapture> ||
                      std::is_same_v<T, SourceConditionalBreak> ||
                      std::is_same_v<T, SourceBreak> ||
                      std::is_same_v<T, SourceConditionalContinue> ||
                      std::is_same_v<T, SourceContinue> ||
                      std::is_same_v<T, SourceReturn> ||
                      std::is_same_v<T, SourceReturnExpression> ||
                      std::is_same_v<T, SourceThrow> ||
                      std::is_same_v<T, SourceInitializerCall>);
      }
    }, node.operation);
  }
}
}  // namespace abcd::source
#endif
