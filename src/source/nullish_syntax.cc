#include "nullish_syntax.h"
#include "tree_checks.h"
#include "expression_children.h"
#include <array>
#include <algorithm>
#include <type_traits>
namespace abcd::source {
namespace {
enum class NullishDomain : std::uint8_t { kNull, kUndefined, kOther, kCount };
constexpr auto kDomains = static_cast<std::size_t>(NullishDomain::kCount);
enum class Truth : std::uint8_t { kFalse = 1, kTrue = 2, kUnknown = 3 };
using Reference = std::variant<SymbolId, FunctionOwnerId>;
using Result = ExprId;
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
bool SameResult(const FunctionTree& tree, const std::vector<TextId>& texts, Result left, Result right) {
  if (left == right) return true;
  const auto a = StableReference(tree, left), b = StableReference(tree, right);
  if (a && b && a == b) return true;
  const auto* x = std::get_if<TreeLiteral>(&tree.expressions[left.value].expression);
  const auto* y = std::get_if<TreeLiteral>(&tree.expressions[right.value].expression);
  if (!x || !y || x->index() != y->index()) return false;
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    const auto& other = std::get<T>(*y);
    if constexpr (std::is_same_v<T, UndefinedLiteral> || std::is_same_v<T, NullLiteral>) return true;
    else if constexpr (std::is_same_v<T, BooleanLiteral> || std::is_same_v<T, IntegerLiteral>) return value.value == other.value;
    else if constexpr (std::is_same_v<T, NumberLiteral>) return value.bits == other.bits;
    else { static_assert(std::is_same_v<T, TextId>); return texts[value.value] == texts[other.value]; }
  }, *x);
}
Meaning Interpret(const FunctionTree& tree, ExprId id, const std::vector<Meaning>& meanings, const std::vector<TextId>& texts) {
  const auto& expression = tree.expressions[id.value].expression;
  Meaning result; result.results.fill(id);
  if (const auto* literal = std::get_if<TreeLiteral>(&expression)) {
    if (std::holds_alternative<UndefinedLiteral>(*literal)) {
      result.predicate = true;
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
  if (std::holds_alternative<MemberExpr>(expression)) {
    // A member is an opaque value/effect leaf. Its base identity is checked
    // only when classifying optional access, never as the fallback's domain.
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
    result = operand; result.results.fill(id);
    for (auto& truth : result.truths) truth = Invert(truth);
    return result;
  }
  if (const auto* logical = std::get_if<LogicalExpr>(&expression)) {
    const auto& left = meanings[logical->left.value]; const auto& right = meanings[logical->right.value];
    if (logical->operation == TreeLogical::kNullish || !left.predicate || !right.predicate ||
        !MergeReference(result, left) || !MergeReference(result, right)) return result;
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
    if (!condition.predicate) return result;
    result.reference = condition.reference;
    if (!result.reference) {
      if (condition.truths[0] == Truth::kTrue) result.reference = yes.reference;
      if (condition.truths[0] == Truth::kFalse) result.reference = no.reference;
    }
    const auto compatible = [&](const Meaning& branch) {
      return !branch.reference || branch.reference == result.reference;
    };
    const auto outcome = [&](const Meaning& branch, ExprId original, std::size_t domain) -> std::optional<Result> {
      // A different binding can still throw on its read (TDZ). Even equal
      // branch results do not authorize erasing that independent predicate.
      return compatible(branch) ? branch.results[domain] : std::optional<Result>{original};
    };
    const auto truth = [&](const Meaning& branch, std::size_t domain) {
      return compatible(branch) ? branch.truths[domain] : Truth::kUnknown;
    };
    result.predicate = true;
    for (std::size_t domain = 0; domain < kDomains; ++domain) {
      const auto choose = condition.truths[domain];
      // Known final truth is insufficient: every actually evaluated guard
      // must be a pure read of this same reference (or a constant). Otherwise
      // a later OR/AND could hide an independent TDZ/receiver exception.
      const bool pure_yes = yes.predicate && compatible(yes), pure_no = no.predicate && compatible(no);
      result.predicate &= choose == Truth::kTrue ? pure_yes : choose == Truth::kFalse ? pure_no : pure_yes && pure_no;
      const auto a = outcome(yes, selection->consequent, domain), b = outcome(no, selection->alternate, domain);
      if (choose == Truth::kTrue) {
        result.results[domain] = a; result.truths[domain] = truth(yes, domain);
      } else if (choose == Truth::kFalse) {
        result.results[domain] = b; result.truths[domain] = truth(no, domain);
      } else {
        // Unknown truth preserves a result only when both alternatives agree.
        // Pure predicate joins compose independently of selection depth/layout.
        result.results[domain] = a && b && SameResult(tree, texts, *a, *b) ? a : std::nullopt;
        result.truths[domain] = truth(yes, domain) == truth(no, domain) ? truth(yes, domain) : Truth::kUnknown;
      }
    }
  }
  return result;
}
}
std::variant<NullishSyntax, TreeStructureError> PlanNullishSyntax(
    const FunctionTree& tree, analysis::SemanticBudget limit) {
  try {
    TreeCheckBudget budget(limit); const auto count = tree.expressions.size();
    NullishSyntax plan; std::vector<Meaning> meanings; std::vector<Inputs> inputs;
    bool selection = false, binding = false, nullish = false;
    for (const auto& node : tree.expressions) {
      budget.Step(); selection |= std::holds_alternative<ConditionalExpr>(node.expression);
      binding |= std::holds_alternative<SymbolExpr>(node.expression) || std::holds_alternative<ThisExpr>(node.expression);
      if (const auto* literal = std::get_if<TreeLiteral>(&node.expression))
        nullish |= std::holds_alternative<NullLiteral>(*literal) || std::holds_alternative<UndefinedLiteral>(*literal);
    }
    if (!selection || !binding || !nullish) { plan.work = budget.Work(); return plan; }
    // Intern UTF-16 values once. Repeated selection joins compare strong IDs,
    // not long shared strings per expression or a literal-pool layout premise.
    std::vector<TextId> texts, ordered;
    budget.Array(texts, tree.texts.size()); budget.Array(ordered, tree.texts.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) ordered[i] = {static_cast<std::uint32_t>(i)};
    std::sort(ordered.begin(), ordered.end(), [&](TextId left, TextId right) {
      const auto& a = tree.texts[left.value]; const auto& b = tree.texts[right.value];
      budget.Step(std::min(a.size(), b.size()) + 1); return a < b;
    });
    std::optional<TextId> canonical;
    for (const auto id : ordered) {
      budget.Step();
      if (canonical) budget.Step(std::min(tree.texts[canonical->value].size(), tree.texts[id.value].size()));
      if (!canonical || tree.texts[canonical->value] != tree.texts[id.value]) canonical = id;
      texts[id.value] = *canonical;
    }
    std::vector<std::optional<ExprId>> parent; std::vector<std::uint8_t> pending;
    std::vector<ExprId> queue;
    budget.Array(plan.projections, count); budget.Array(plan.omitted_by, count);
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
      const auto id = queue[at]; budget.Step(); meanings[id.value] = Interpret(tree, id, meanings, texts);
      const auto& meaning = meanings[id.value];
      const auto nil = meaning.results[static_cast<std::size_t>(NullishDomain::kNull)];
      const auto absent = meaning.results[static_cast<std::size_t>(NullishDomain::kUndefined)];
      const auto other = meaning.results[static_cast<std::size_t>(NullishDomain::kOther)];
      if (std::holds_alternative<ConditionalExpr>(tree.expressions[id.value].expression) &&
          meaning.reference && nil && absent && other && SameResult(tree, texts, *nil, *absent)) {
        const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[nil->value].expression);
        const auto* member = std::get_if<MemberExpr>(&tree.expressions[other->value].expression);
        if (literal && std::holds_alternative<UndefinedLiteral>(*literal) && member &&
            StableReference(tree, member->base) == meaning.reference)
          plan.projections[id.value] = OptionalMemberSyntax{*other};
        else if (StableReference(tree, *other) == meaning.reference && !SameResult(tree, texts, *nil, *other))
          plan.projections[id.value] = NullishFallbackSyntax{*other, *nil};
      }
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
          plan.projections[id.value] ? std::optional{id} : std::nullopt;
      for (std::uint8_t child = 0; child < inputs[id.value].count; ++child) {
        budget.Step(); const auto next = inputs[id.value].ids[child];
        if (selected && !plan.Retains(*selected, next)) plan.omitted_by[next.value] = selected;
        queue[ready++] = next;
      }
      if (plan.omitted_by[id.value]) plan.projections[id.value].reset();
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
        if (plan.Retains(root, child) || plan.omitted_by[child.value]) return;
        plan.omitted_by[child.value] = root; plan.projections[child.value].reset(); queue[ready++] = child;
      });
      if (const auto* object = std::get_if<ObjectExpr>(&expression))
        for (std::uint32_t i = 0; i < object->properties.count; ++i) {
          budget.Step(); plan.omitted_properties_by[object->properties.first + i] = root;
        }
    }
    plan.allocated_bytes = budget.Bytes(); plan.work = budget.Work(); return plan;
  } catch (TreeStructureError error) { return error; }
}
bool NullishSyntax::Retains(ExprId root, ExprId child) const {
  return std::visit([&](const auto& syntax) {
    using T = std::decay_t<decltype(syntax)>;
    if constexpr (std::is_same_v<T, OptionalMemberSyntax>) return syntax.member == child;
    else { static_assert(std::is_same_v<T, NullishFallbackSyntax>); return syntax.value == child || syntax.fallback == child; }
  }, *projections[root.value]);
}
ExprId NullishSyntax::Base(const FunctionTree& tree, ExprId root) const {
  return std::visit([&](const auto& syntax) {
    using T = std::decay_t<decltype(syntax)>;
    if constexpr (std::is_same_v<T, OptionalMemberSyntax>) return std::get<MemberExpr>(tree.expressions[syntax.member.value].expression).base;
    else { static_assert(std::is_same_v<T, NullishFallbackSyntax>); return syntax.value; }
  }, *projections[root.value]);
}
NullishForm NullishSyntax::Form(ExprId root) const {
  return std::holds_alternative<OptionalMemberSyntax>(*projections[root.value]) ? NullishForm::kOptionalMember : NullishForm::kFallback;
}
}
