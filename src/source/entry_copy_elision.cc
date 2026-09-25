#include "entry_copy_elision.h"

#include <algorithm>
#include <type_traits>

namespace abcd::source {
namespace {
bool HasBoundReads(const SourceExpression& expression) {
  return std::visit([](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarExpression> ||
                  std::is_same_v<T, MemberExpression> ||
                  std::is_same_v<T, StringExpression> ||
                  std::is_same_v<T, StringComparison> ||
                  std::is_same_v<T, ModuleExpression> ||
                  std::is_same_v<T, LocalExpression> ||
                  std::is_same_v<T, analysis::EmptyObjectCreation> ||
                  std::is_same_v<T, CallExpression> ||
                  std::is_same_v<T, MemberCallExpression> ||
                  std::is_same_v<T, analysis::ConstructorCall>) {
      return true;
    } else {
      // Creation/capture and external-name consumers retain identifier-based
      // paths. Never infer their reads from the already generated source.
      static_assert(std::is_same_v<T, FunctionExpression> ||
                    std::is_same_v<T, ClassExpression> ||
                    std::is_same_v<T, MethodExpression> ||
                    std::is_same_v<T, AccessorExpression> ||
                    std::is_same_v<T, CapturedExpression> ||
                    std::is_same_v<T, GlobalExpression> ||
                    std::is_same_v<T, analysis::GlobalObjectLoad> ||
                    std::is_same_v<T, analysis::BufferedObjectCreation> ||
                    std::is_same_v<T, analysis::BufferedArrayCreation> ||
                    std::is_same_v<T, analysis::AwaitValue> ||
                    std::is_same_v<T, analysis::SuperRange> ||
                    std::is_same_v<T, analysis::RestCopy>);
      return false;
    }
  }, expression);
}
}  // namespace

bool CanElideEntryCopies(const SourceAst& ast) {
  if (ast.argument_region || ast.regions.catches) return false;
  return std::all_of(ast.statements.begin(), ast.statements.end(), [](const auto& statement) {
    return std::visit([](const auto& operation) {
      using T = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<T, SourceAssignment> ||
                    std::is_same_v<T, SourceLocalAssignment> ||
                    std::is_same_v<T, SourceReturnExpression>) {
        return HasBoundReads(operation.value);
      } else if constexpr (std::is_same_v<T, analysis::NoOperation> ||
                           std::is_same_v<T, SourceMemberStore> ||
                           std::is_same_v<T, SourceOwnStore> ||
                           std::is_same_v<T, SourceLocalStore> ||
                           std::is_same_v<T, SourceModuleStore> ||
                           std::is_same_v<T, SourceConditionalBreak> ||
                           std::is_same_v<T, SourceConditionalContinue> ||
                           std::is_same_v<T, SourceBreak> ||
                           std::is_same_v<T, SourceContinue> ||
                           std::is_same_v<T, SourceReturn> ||
                           std::is_same_v<T, SourceThrow>) {
        return true;
      } else {
        static_assert(std::is_same_v<T, SourceMethodStore> ||
                      std::is_same_v<T, SourceIndexedStore> ||
                      std::is_same_v<T, SourceArrayCapture> ||
                      std::is_same_v<T, SourceArrayCommit> ||
                      std::is_same_v<T, SourceArrayLiteral> ||
                      std::is_same_v<T, SourceStagedCall> ||
                      std::is_same_v<T, SourceInitializerCall>);
        return false;
      }
    }, statement.operation);
  });
}

bool IsBoundEntryCopy(const SourceStatement& statement,
                      std::span<const EntryBinding> bindings) {
  if (statement.fused_read || statement.fused_spill || statement.fused_reload ||
      statement.class_origin || statement.previous_origin) return false;
  const auto* assignment = std::get_if<SourceAssignment>(&statement.operation);
  const auto* target = assignment ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
  const auto* expression = assignment
      ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
  const auto* value = expression ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
  const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
  return target && source && target->value < bindings.size() &&
         bindings[target->value].source == *source;
}
}  // namespace abcd::source
