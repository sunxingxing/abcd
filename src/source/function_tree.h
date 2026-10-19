#ifndef ABCD_SOURCE_FUNCTION_TREE_H_
#define ABCD_SOURCE_FUNCTION_TREE_H_

#include "capture_tree.h"
#include "class_self_scope.h"
#include "completion_plan.h"
#include "control_plan.h"
#include "expression_forest.h"
#include "function_syntax.h"
#include "tree_flow.h"
#include "tree_semantics.h"

namespace abcd::source {
struct ConstructedFunction {
  FunctionTree tree;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Absent only for explicitly partial trees; never fabricate correspondence.
  std::optional<TreeFlow> flow;
};
enum class FunctionTreeError : std::uint8_t {
  kControl, kExpression, kBinding, kName, kOrigin, kOwnership,
  kResourceLimit, kWorkLimit, kStructure, kSemantics, kCapture
};
// A function-tree refusal with a bounded typed obligation/location and the
// actual failed verifier cause. These fields never influence admission.
struct FunctionTreeFailure {
  FunctionTreeError kind;
  std::variant<std::monostate, ExpressionFailure, ControlError, ExceptionControlError, CompletionError,
      TreeFlowError, TreeStructureError, TreeSemanticFailure, EvaluationFailure> cause;
  std::optional<TreeFailureEvidence> tree;
  FunctionTreeFailure(FunctionTreeError value) : kind(value) {}  // NOLINT: implicit kind.
  std::optional<abc::EntityOffset> method;
  FunctionTreeFailure(FunctionTreeError value, ExpressionFailure expression,
      std::optional<abc::EntityOffset> target = {}) : kind(value), cause(expression), method(target) {}
  bool operator==(FunctionTreeError value) const { return kind == value; }
};
// Builds syntax checked against IR/control/evaluation and resolved constants.
// Caller establishes callable/strictness from creation evidence; construction
// does not certify those facts or authorize publishing.
// Existing graph/plans/constants are caller-owned budget; syntax and all
// construction-owned output/scratch share this call's byte/work allowance.
// An incoming creation derives capture aliases from the actual checked parent
// tree and verifies the resulting child relationship. Caller-supplied captures
// must be empty in this mode. This does not close transitive family premises.
// AllowPartial currently retains closed expression roots before an unsupported
// lowering in one normal straight-line region without cells/phis/children.
// The suffix becomes an explicit gap; malformed inputs remain errors. Other
// topologies retain complete-only behavior, never a guessed linearization.
[[nodiscard]] std::variant<ConstructedFunction, FunctionTreeFailure>
BuildFunctionTree(const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, FunctionSyntax syntax,
    ExpressionConstants constants, analysis::SemanticBudget budget = {},
    const IncomingCaptureTree* incoming = nullptr,
    RecoveryMode recovery = RecoveryMode::kCompleteOnly);
struct ClassInitializerTreeInput {
  const ClassSelfInitializationInput& original;
  const ClassSelfInitialization& initialization;
  const ClassSelfScope& scope;
  std::uint32_t family, member;
  NameId name;
};
// Statement/expression construction uses the common builder. This type cannot
// be wrapped as ConstructedFunction: enclosing captures and original-flow
// class-initialization ports remain independent publication obligations.
struct ClassInitializerTreeDraft {
  FunctionTree tree;
  BindingPlan bindings;
  std::vector<std::optional<ExprId>> expressions;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
std::variant<ClassInitializerTreeDraft, FunctionTreeFailure>
BuildClassInitializerTreeDraft(const analysis::ValueProgram&,
                               const ControlPlan&, const EvaluationPlan&,
                               FunctionSyntax, ExpressionConstants,
                               const ClassInitializerTreeInput&,
                               analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
