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
struct Formation {
  std::optional<ExprId> expression;
  bool preceding_read = false;
  bool valid = true;
};
Formation Follow(Formation first, Formation second) {
  // Native access retains the original reference-forming expression in its
  // entirety. Never move it ahead of an old-value read or discard a separate
  // write. These are ordering/value obligations, not guard-layout limits.
  if (second.expression && (first.preceding_read || first.expression)) first.valid = false;
  if (!first.expression) {
    first.expression = second.expression;
    first.preceding_read |= second.preceding_read;
  }
  first.valid &= second.valid;
  return first;
}
Formation Join(Formation left, Formation right) {
  left.valid &= right.valid && left.expression == right.expression;
  left.preceding_read |= right.preceding_read;
  return left;
}
struct Meaning {
  std::optional<Reference> reference;
  std::array<Truth, kDomains> truths{Truth::kUnknown, Truth::kUnknown, Truth::kUnknown};
  std::array<std::optional<Result>, kDomains> results;
  std::array<Formation, kDomains> formations;
  bool predicate = false;
};
struct MemberPath {
  std::optional<Reference> reference;
  std::optional<ExprId> access;
};
// Dependency arity is the three fields of ConditionalExpr, not a corpus limit.
constexpr std::size_t kConditionalArity = 3;
struct Inputs { std::array<ExprId, kConditionalArity> ids; std::uint8_t count = 0; };
Inputs Children(const TreeExpression& expression) {
  return std::visit([](const auto& node) -> Inputs {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, ConditionalExpr>) return {{{node.condition, node.consequent, node.alternate}}, 3};
    else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, LogicalExpr> ||
        std::is_same_v<T, SequenceExpr>) return {{{node.left, node.right}}, 2};
    else if constexpr (std::is_same_v<T, UnaryExpr>) return {{{node.operand}}, 1};
    else if constexpr (std::is_same_v<T, MemberExpr>) return {{{node.base}}, 1};
    else if constexpr (std::is_same_v<T, CallExpr>) return {{{node.callee}}, 1};
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
std::optional<Reference> FormedReference(const FunctionTree& tree, ExprId id) {
  if (const auto stable = StableReference(tree, id)) return stable;
  if (const auto* assignment = std::get_if<AssignmentExpr>(&tree.expressions[id.value].expression))
    if (const auto* target = std::get_if<SymbolTarget>(&assignment->target)) return Reference{target->symbol};
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
    for (auto& formation : result.formations) formation.preceding_read = true;
    result.truths = {Truth::kFalse, Truth::kFalse, Truth::kUnknown}; return result;
  }
  if (const auto reference = FormedReference(tree, id)) {
    // Assignment returns its RHS and stores that same value. Keep this actual
    // occurrence, including arbitrary RHS effects and the target's scope check;
    // subsequent pure reads observe its saved value. No producer whitelist.
    result.reference = reference; result.predicate = true;
    for (auto& formation : result.formations) formation.expression = id;
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
    auto input = binary->left;
    auto reference = FormedReference(tree, input);
    auto constant = binary->right;
    if (!reference) { input = binary->right; reference = FormedReference(tree, input); constant = binary->left; }
    if (!reference) return result;
    const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[constant.value].expression);
    if (!literal) return result;
    std::optional<NullishDomain> equal;
    if (std::holds_alternative<NullLiteral>(*literal)) equal = NullishDomain::kNull;
    if (std::holds_alternative<UndefinedLiteral>(*literal)) equal = NullishDomain::kUndefined;
    if (!equal) return result;
    result.reference = reference; result.predicate = true;
    result.formations = meanings[input.value].formations;
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
      const auto stop = logical->operation == TreeLogical::kAnd ? Truth::kFalse : Truth::kTrue;
      result.formations[domain] = a == stop ? left.formations[domain] : a == Truth::kUnknown ?
          Join(left.formations[domain], Follow(left.formations[domain], right.formations[domain])) :
          Follow(left.formations[domain], right.formations[domain]);
    }
    return result;
  }
  if (const auto* sequence = std::get_if<SequenceExpr>(&expression)) {
    const auto& left = meanings[sequence->left.value]; const auto& right = meanings[sequence->right.value];
    if (!left.predicate || !right.predicate || !MergeReference(result, left) || !MergeReference(result, right)) return result;
    result.predicate = true; result.results = right.results; result.truths = right.truths;
    for (std::size_t domain = 0; domain < kDomains; ++domain)
      result.formations[domain] = Follow(left.formations[domain], right.formations[domain]);
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
      // Known final truth is insufficient: guards must belong to this same
      // reference domain. Ordered formation separately retains their effects;
      // foreign reads cannot lose a TDZ/receiver exception through OR/AND.
      const bool pure_yes = yes.predicate && compatible(yes), pure_no = no.predicate && compatible(no);
      result.predicate &= choose == Truth::kTrue ? pure_yes : choose == Truth::kFalse ? pure_no : pure_yes && pure_no;
      const auto a = outcome(yes, selection->consequent, domain), b = outcome(no, selection->alternate, domain);
      result.formations[domain] = Follow(condition.formations[domain], choose == Truth::kTrue ? yes.formations[domain] :
          choose == Truth::kFalse ? no.formations[domain] : Join(yes.formations[domain], no.formations[domain]));
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
    std::vector<MemberPath> paths;
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
    budget.Array(paths, count);
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
      if (const auto* member = std::get_if<MemberExpr>(&tree.expressions[id.value].expression)) {
        // An original member spine evaluates each base/key/getter in order.
        // Summarize its stable starting reference bottom-up once, rather than
        // replaying every candidate's chain. Arbitrary keys stay opaque/owned;
        // the guard skips all of them only when its original receiver is nil.
        if (const auto reference = StableReference(tree, member->base)) paths[id.value] = {reference, id};
        else paths[id.value] = paths[member->base.value];
      }
      const auto& meaning = meanings[id.value];
      const auto nil = meaning.results[static_cast<std::size_t>(NullishDomain::kNull)];
      const auto absent = meaning.results[static_cast<std::size_t>(NullishDomain::kUndefined)];
      const auto other = meaning.results[static_cast<std::size_t>(NullishDomain::kOther)];
      bool ordered = true;
      const auto formed = meaning.formations[0].expression;
      for (const auto& formation : meaning.formations)
        ordered &= formation.valid && formation.expression == formed;
      if (std::holds_alternative<ConditionalExpr>(tree.expressions[id.value].expression) &&
          ordered && meaning.reference && nil && absent && other && SameResult(tree, texts, *nil, *absent)) {
        const auto* literal = std::get_if<TreeLiteral>(&tree.expressions[nil->value].expression);
        const auto* member = std::get_if<MemberExpr>(&tree.expressions[other->value].expression);
        const auto* call = std::get_if<CallExpr>(&tree.expressions[other->value].expression);
        if (literal && std::holds_alternative<UndefinedLiteral>(*literal) && member &&
            paths[other->value].access && paths[other->value].reference == meaning.reference) {
          const auto access = *paths[other->value].access;
          const auto base = std::get<MemberExpr>(tree.expressions[access.value].expression).base;
          plan.projections[id.value] = OptionalMemberSyntax{*other, formed.value_or(base), access};
        }
        else if (literal && std::holds_alternative<UndefinedLiteral>(*literal) && call) {
          // Optional evaluation must skip the entire original call (including
          // arguments) on nullish paths. Reference identity, not callee spelling
          // or a compiler guard layout, determines receiver and indirect eval.
          if (call->reference == CallReference::kValue && StableReference(tree, call->callee) == meaning.reference)
            plan.projections[id.value] = OptionalCallSyntax{*other, formed.value_or(call->callee), {}, {}};
          else if (call->reference == CallReference::kMember) {
            const auto* callee = std::get_if<MemberExpr>(&tree.expressions[call->callee.value].expression);
            if (callee && paths[call->callee.value].access && paths[call->callee.value].reference == meaning.reference) {
              const auto access = *paths[call->callee.value].access;
              const auto base = std::get<MemberExpr>(tree.expressions[access.value].expression).base;
              plan.projections[id.value] = OptionalCallSyntax{*other, formed.value_or(base), call->callee, access};
            }
          }
        }
        else if (StableReference(tree, *other) == meaning.reference && !SameResult(tree, texts, *nil, *other))
          plan.projections[id.value] = NullishFallbackSyntax{formed.value_or(*other), *nil};
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
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step(); if (!plan.projections[i]) continue;
      if (const auto* member = std::get_if<OptionalMemberSyntax>(&*plan.projections[i])) {
        const auto base = std::get<MemberExpr>(tree.expressions[member->access.value].expression).base;
        if (base != member->base) plan.omitted_by[base.value] = ExprId{static_cast<std::uint32_t>(i)};
      } else if (const auto* call = std::get_if<OptionalCallSyntax>(&*plan.projections[i])) {
        const auto base = call->member ? std::get<MemberExpr>(tree.expressions[call->access->value].expression).base :
            std::get<CallExpr>(tree.expressions[call->call.value].expression).callee;
        if (base != call->base) plan.omitted_by[base.value] = ExprId{static_cast<std::uint32_t>(i)};
      }
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
    if constexpr (std::is_same_v<T, OptionalMemberSyntax>) return syntax.member == child || syntax.base == child;
    else if constexpr (std::is_same_v<T, OptionalCallSyntax>) return syntax.call == child || syntax.member == child || syntax.base == child;
    else { static_assert(std::is_same_v<T, NullishFallbackSyntax>); return syntax.value == child || syntax.fallback == child; }
  }, *projections[root.value]);
}
ExprId NullishSyntax::Base(const FunctionTree&, ExprId root) const {
  return std::visit([&](const auto& syntax) {
    using T = std::decay_t<decltype(syntax)>;
    if constexpr (std::is_same_v<T, OptionalMemberSyntax> || std::is_same_v<T, OptionalCallSyntax>) return syntax.base;
    else { static_assert(std::is_same_v<T, NullishFallbackSyntax>); return syntax.value; }
  }, *projections[root.value]);
}
NullishForm NullishSyntax::Form(ExprId root) const {
  return std::visit([](const auto& syntax) {
    using T = std::decay_t<decltype(syntax)>;
    if constexpr (std::is_same_v<T, OptionalMemberSyntax>) return NullishForm::kOptionalMember;
    else if constexpr (std::is_same_v<T, OptionalCallSyntax>) return NullishForm::kOptionalCall;
    else { static_assert(std::is_same_v<T, NullishFallbackSyntax>); return NullishForm::kFallback; }
  }, *projections[root.value]);
}
}
