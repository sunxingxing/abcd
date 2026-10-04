#include "optional_access.h"
#include "tree_checks.h"
#include "expression_children.h"
#include <array>
#include <type_traits>
namespace abcd::source {
namespace {
enum class NullishDomain : std::uint8_t { kNull, kUndefined, kOther, kCount };
constexpr auto kDomains = static_cast<std::size_t>(NullishDomain::kCount);
enum class Truth : std::uint8_t { kFalse = 1, kTrue = 2, kUnknown = 3 };
using Reference = std::variant<SymbolId, FunctionOwnerId>;
struct UndefinedResult { bool operator==(const UndefinedResult&) const = default; };
using Result = std::variant<UndefinedResult, ExprId>;
struct Meaning {
  std::optional<Reference> reference;
  std::array<Truth, kDomains> truths{Truth::kUnknown, Truth::kUnknown, Truth::kUnknown};
  std::array<std::optional<Result>, kDomains> results;
  bool predicate = false;
};
// Dependency arity is the three fields of ConditionalExpr, not a corpus limit.
constexpr std::size_t kConditionalArity = 3;
struct Inputs { std::array<ExprId, kConditionalArity> ids; std::uint8_t count = 0; };
Inputs Children(const TreeExpression& expression) {
  return std::visit([](const auto& node) -> Inputs {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, ConditionalExpr>) return {{{node.condition, node.consequent, node.alternate}}, 3};
    else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, LogicalExpr>) return {{{node.left, node.right}}, 2};
    else if constexpr (std::is_same_v<T, UnaryExpr>) return {{{node.operand}}, 1};
    else return {};
  }, expression);
}
std::optional<Reference> StableReference(const FunctionTree& tree, ExprId id) {
  if (id.value >= tree.expressions.size()) return {};
  const auto& expression = tree.expressions[id.value].expression;
  if (const auto* symbol = std::get_if<SymbolExpr>(&expression)) return Reference{symbol->symbol};
  if (const auto* receiver = std::get_if<ThisExpr>(&expression)) return Reference{receiver->owner};
  return {};
}
bool MergeReference(Meaning& result, const Meaning& input) {
  if (!input.reference) return true;
  if (result.reference && result.reference != input.reference) return false;
  result.reference = input.reference; return true;
}
Truth Invert(Truth truth) {
  if (truth == Truth::kTrue) return Truth::kFalse;
  if (truth == Truth::kFalse) return Truth::kTrue;
  return Truth::kUnknown;
}
Meaning Interpret(const FunctionTree& tree, ExprId id, const std::vector<Meaning>& meanings) {
  const auto& expression = tree.expressions[id.value].expression;
  Meaning result;
  if (const auto* literal = std::get_if<TreeLiteral>(&expression)) {
    if (std::holds_alternative<UndefinedLiteral>(*literal)) {
      result.results.fill(Result{UndefinedResult{}}); result.predicate = true;
      result.truths.fill(Truth::kFalse);
    } else if (std::holds_alternative<NullLiteral>(*literal)) {
      result.predicate = true; result.truths.fill(Truth::kFalse);
    } else if (const auto* boolean = std::get_if<BooleanLiteral>(literal)) {
      result.predicate = true; result.truths.fill(boolean->value ? Truth::kTrue : Truth::kFalse);
    }
    return result;
  }
  if (const auto reference = StableReference(tree, id)) {
    result.reference = reference; result.predicate = true;
    result.truths = {Truth::kFalse, Truth::kFalse, Truth::kUnknown}; return result;
  }
  if (const auto* member = std::get_if<MemberExpr>(&expression)) {
    result.reference = StableReference(tree, member->base);
    if (result.reference) result.results.fill(Result{id});
    return result;
  }
  if (const auto* binary = std::get_if<BinaryExpr>(&expression)) {
    if (binary->operation != analysis::BinaryOperator::kStrictEqual &&
        binary->operation != analysis::BinaryOperator::kStrictNotEqual) return result;
    auto reference = StableReference(tree, binary->left);
    auto constant = binary->right;
    if (!reference) { reference = StableReference(tree, binary->right); constant = binary->left; }
    if (!reference) return result;
    const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[constant.value].expression);
    if (!literal) return result;
    std::optional<NullishDomain> equal;
    if (std::holds_alternative<NullLiteral>(*literal)) equal = NullishDomain::kNull;
    if (std::holds_alternative<UndefinedLiteral>(*literal)) equal = NullishDomain::kUndefined;
    if (!equal) return result;
    result.reference = reference; result.predicate = true;
    for (std::size_t domain = 0; domain < kDomains; ++domain) {
      const bool matches = domain == static_cast<std::size_t>(*equal);
      const bool positive = binary->operation == analysis::BinaryOperator::kStrictEqual;
      result.truths[domain] = matches == positive ? Truth::kTrue : Truth::kFalse;
    }
    return result;
  }
  if (const auto* unary = std::get_if<UnaryExpr>(&expression)) {
    const auto& operand = meanings[unary->operand.value];
    if (unary->operation != TreeUnary::kNot || !operand.predicate) return result;
    result = operand; result.results = {};
    for (auto& truth : result.truths) truth = Invert(truth);
    return result;
  }
  if (const auto* logical = std::get_if<LogicalExpr>(&expression)) {
    const auto& left = meanings[logical->left.value]; const auto& right = meanings[logical->right.value];
    if (logical->operation == TreeLogical::kNullish || !left.predicate || !right.predicate ||
        !MergeReference(result, left) || !MergeReference(result, right)) return {};
    result.predicate = true;
    for (std::size_t domain = 0; domain < kDomains; ++domain) {
      const auto a = left.truths[domain], b = right.truths[domain];
      if (logical->operation == TreeLogical::kAnd)
        result.truths[domain] = a == Truth::kFalse || b == Truth::kFalse ? Truth::kFalse :
            a == Truth::kTrue && b == Truth::kTrue ? Truth::kTrue : Truth::kUnknown;
      else result.truths[domain] = a == Truth::kTrue || b == Truth::kTrue ? Truth::kTrue :
            a == Truth::kFalse && b == Truth::kFalse ? Truth::kFalse : Truth::kUnknown;
    }
    return result;
  }
  if (const auto* selection = std::get_if<ConditionalExpr>(&expression)) {
    const auto& condition = meanings[selection->condition.value];
    const auto& yes = meanings[selection->consequent.value]; const auto& no = meanings[selection->alternate.value];
    if (!condition.predicate || !MergeReference(result, condition) ||
        !MergeReference(result, yes) || !MergeReference(result, no)) return {};
    result.predicate = yes.predicate && no.predicate;
    for (std::size_t domain = 0; domain < kDomains; ++domain) {
      const auto truth = condition.truths[domain];
      if (truth == Truth::kTrue) {
        result.results[domain] = yes.results[domain]; result.truths[domain] = yes.truths[domain];
      } else if (truth == Truth::kFalse) {
        result.results[domain] = no.results[domain]; result.truths[domain] = no.truths[domain];
      } else {
        // Unknown truth preserves a result only when both alternatives agree.
        // Pure predicate joins compose independently of selection depth/layout.
        if (yes.results[domain] == no.results[domain]) result.results[domain] = yes.results[domain];
        result.truths[domain] = yes.truths[domain] == no.truths[domain] ? yes.truths[domain] : Truth::kUnknown;
      }
    }
  }
  return result;
}
}
std::variant<OptionalAccessSyntax, TreeStructureError> PlanOptionalAccess(
    const FunctionTree& tree, analysis::SemanticBudget limit) {
  try {
    TreeCheckBudget budget(limit); const auto count = tree.expressions.size();
    OptionalAccessSyntax plan; std::vector<Meaning> meanings; std::vector<Inputs> inputs;
    bool selection = false, member = false;
    for (const auto& node : tree.expressions) {
      budget.Step(); selection |= std::holds_alternative<ConditionalExpr>(node.expression);
      if (const auto* read = std::get_if<MemberExpr>(&node.expression)) member |= bool(StableReference(tree, read->base));
    }
    if (!selection || !member) { plan.work = budget.Work(); return plan; }
    std::vector<std::optional<ExprId>> parent; std::vector<std::uint8_t> pending;
    std::vector<ExprId> queue;
    budget.Array(plan.members, count); budget.Array(plan.omitted_by, count);
    budget.Array(plan.omitted_properties_by, tree.properties.size());
    budget.Array(meanings, count); budget.Array(inputs, count); budget.Array(parent, count);
    budget.Array(pending, count); budget.Array(queue, count);
    std::size_t ready = 0;
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step(); inputs[i] = Children(tree.expressions[i].expression); pending[i] = inputs[i].count;
      for (std::uint8_t child = 0; child < inputs[i].count; ++child) {
        const auto id = inputs[i].ids[child]; budget.Step();
        RequireTree(id.value < count && !parent[id.value], TreeStructureError::kOwnership);
        parent[id.value] = ExprId{static_cast<std::uint32_t>(i)};
      }
      if (!pending[i]) queue[ready++] = {static_cast<std::uint32_t>(i)};
    }
    for (std::size_t at = 0; at < ready; ++at) {
      const auto id = queue[at]; budget.Step(); meanings[id.value] = Interpret(tree, id, meanings);
      const auto& meaning = meanings[id.value];
      if (std::holds_alternative<ConditionalExpr>(tree.expressions[id.value].expression) &&
          meaning.results[static_cast<std::size_t>(NullishDomain::kNull)] &&
          meaning.results[static_cast<std::size_t>(NullishDomain::kUndefined)] &&
          meaning.results[static_cast<std::size_t>(NullishDomain::kOther)] &&
          std::holds_alternative<UndefinedResult>(*meaning.results[static_cast<std::size_t>(NullishDomain::kNull)]) &&
          std::holds_alternative<UndefinedResult>(*meaning.results[static_cast<std::size_t>(NullishDomain::kUndefined)]))
        if (const auto* member = std::get_if<ExprId>(&*meaning.results[static_cast<std::size_t>(NullishDomain::kOther)]))
          plan.members[id.value] = *member;
      if (parent[id.value] && !--pending[parent[id.value]->value]) queue[ready++] = *parent[id.value];
    }
    RequireTree(ready == count, TreeStructureError::kOwnership);
    // Choose outermost candidates in one top-down pass. A selected member and
    // its computed-key subtree remain independently printed, never absorbed.
    ready = 0;
    for (std::size_t i = 0; i < count; ++i) if (!parent[i]) queue[ready++] = {static_cast<std::uint32_t>(i)};
    for (std::size_t at = 0; at < ready; ++at) {
      budget.Step(); const auto id = queue[at];
      const auto selected = plan.omitted_by[id.value] ? plan.omitted_by[id.value] :
          plan.members[id.value] ? std::optional{id} : std::nullopt;
      for (std::uint8_t child = 0; child < inputs[id.value].count; ++child) {
        budget.Step(); const auto next = inputs[id.value].ids[child];
        if (selected && plan.members[selected->value] != next) plan.omitted_by[next.value] = selected;
        queue[ready++] = next;
      }
      if (plan.omitted_by[id.value]) plan.members[id.value].reset();
    }
    // Nullish abstract interpretation can prove an opaque arm unreachable.
    // Its entire syntax ownership still needs event ranges, including variadic
    // call/aggregate children. Each omitted node is queued once, not per root.
    ready = 0;
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step(); if (plan.omitted_by[i]) queue[ready++] = {static_cast<std::uint32_t>(i)};
    }
    for (std::size_t at = 0; at < ready; ++at) {
      budget.Step(); const auto id = queue[at]; const auto root = *plan.omitted_by[id.value];
      const auto& expression = tree.expressions[id.value].expression;
      VisitExpressionChildren(tree, expression, [&](ExprId child) {
        budget.Step();
        if (child == plan.members[root.value] || plan.omitted_by[child.value]) return;
        plan.omitted_by[child.value] = root; plan.members[child.value].reset(); queue[ready++] = child;
      });
      if (const auto* object = std::get_if<ObjectExpr>(&expression))
        for (std::uint32_t i = 0; i < object->properties.count; ++i) {
          budget.Step(); plan.omitted_properties_by[object->properties.first + i] = root;
        }
    }
    plan.allocated_bytes = budget.Bytes(); plan.work = budget.Work(); return plan;
  } catch (TreeStructureError error) { return error; }
}
}
