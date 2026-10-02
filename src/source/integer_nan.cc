#include "integer_nan.h"
#include "../analysis/register_value_rule.h"
#include "../analysis/value_edits.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
bool IntegerCoercion(const analysis::ValueOperation& operation) {
  if (const auto* unary = std::get_if<analysis::ValueUnary>(&operation)) return unary->op == analysis::UnaryOperator::kBitNot;
  const auto* binary = std::get_if<analysis::ValueBinary>(&operation);
  if (!binary) return false;
  using B = analysis::BinaryOperator;
  switch (binary->op) {
    case B::kBitAnd: case B::kBitOr: case B::kBitXor:
    case B::kShiftLeft: case B::kShiftRightSigned: case B::kShiftRightUnsigned: return true;
    case B::kAdd: case B::kSubtract: case B::kMultiply: case B::kDivide:
    case B::kRemainder: case B::kExponent: case B::kEqual: case B::kNotEqual:
    case B::kLess: case B::kLessEqual: case B::kGreater: case B::kGreaterEqual:
    case B::kStrictEqual: case B::kStrictNotEqual: case B::kIn: case B::kInstanceOf: return false;
  }
  return false;
}
bool ErasableNaN(const analysis::ValueOperand& operand) {
  const auto* number = std::get_if<analysis::Float64Bits>(&operand);
  if (!number || number->value == CanonicalNaNBits()) return false;
  const auto kinds = analysis::detail::FloatNumberKinds(*number);
  return kinds.IsNumber() && !kinds.IsNonNaNNumber();
}
namespace {
using Error = EvaluationError;
constexpr std::uint8_t kIntegerObservation = 1, kOtherObservation = 2;
void Require(bool valid) { if (!valid) throw Error::kOwnership; }
struct Quota {
  analysis::SemanticBudget left;
  explicit Quota(analysis::SemanticBudget budget) : left(budget) {}
  void Step(std::uint64_t n = 1) { if (n > left.work) throw Error::kWorkLimit; left.work -= n; }
  template<class T> void Array(std::vector<T>& array, std::size_t n) {
    if (n > left.bytes / sizeof(T)) throw Error::kResourceLimit;
    left.bytes -= n * sizeof(T); array.resize(n); Step(n);
    const auto extra = array.capacity() - n;
    if (extra > left.bytes / sizeof(T)) throw Error::kResourceLimit;
    left.bytes -= extra * sizeof(T);
  }
};
bool CopyIdentity(const analysis::ValueProgram& graph, const analysis::ValueOp& op) {
  return op.life == analysis::ValueLife::kLive && op.result && op.use_count == 1 &&
      std::holds_alternative<analysis::ValueCopy>(op.operation) && analysis::CanErasePureOp(op) &&
      op.first_use.value < graph.uses.size() &&
      std::holds_alternative<analysis::ValueId>(graph.uses[op.first_use.value].operand);
}
// Alias identities are already checked. One census of real observations, not
// a per-constant traversal: only integer coercion may discard a NaN payload.
std::vector<std::uint8_t> Observations(const analysis::ValueProgram& graph, const EvaluationPlan& plan, Quota& quota) {
  std::vector<std::uint8_t> observed; quota.Array(observed, graph.values.size());
  std::vector<analysis::ValueId> roots, pending;
  enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
  std::vector<Visit> visits;
  quota.Array(roots,graph.values.size()); quota.Array(pending,graph.values.size()); quota.Array(visits,graph.values.size());
  for (std::size_t i=0;i<roots.size();++i) { quota.Step(); roots[i] = {static_cast<std::uint32_t>(i)}; }
  for (const auto& op : graph.ops) {
    quota.Step(); if (!CopyIdentity(graph,op)) continue;
    const auto source = std::get<analysis::ValueId>(graph.uses[op.first_use.value].operand);
    Require(op.result->value < roots.size() && source.value < roots.size()); roots[op.result->value] = source;
  }
  // Exact copies preserve payload even when materialized in the normal route.
  // Compress the functional graph once; never walk a chain for every use.
  for (std::size_t i=0;i<roots.size();++i) {
    quota.Step(); if (visits[i] == Visit::kDone) continue;
    std::size_t count=0; analysis::ValueId cursor{static_cast<std::uint32_t>(i)};
    while (visits[cursor.value] == Visit::kUnseen && roots[cursor.value] != cursor) {
      quota.Step(); visits[cursor.value]=Visit::kActive; pending[count++]=cursor; cursor=roots[cursor.value];
    }
    Require(visits[cursor.value] != Visit::kActive);
    const auto root=roots[cursor.value]; visits[cursor.value]=Visit::kDone;
    for (std::size_t j=0;j<count;++j) { quota.Step(); roots[pending[j].value]=root; visits[pending[j].value]=Visit::kDone; }
  }
  for (const auto& use : graph.uses) {
    quota.Step(); if (use.life != analysis::ValueLife::kLive) continue;
    const auto* value = std::get_if<analysis::ValueId>(&use.operand); if (!value) continue;
    Require(value->value < graph.values.size());
    const auto root = roots[value->value]; Require(root.value < observed.size());
    const auto* owner = std::get_if<analysis::OpId>(&use.owner);
    bool integer = false;
    if (owner) {
      Require(owner->value < graph.ops.size() && owner->value < plan.operations.size());
      const auto& op = graph.ops[owner->value];
      if (op.life != analysis::ValueLife::kLive || CopyIdentity(graph,op) || plan.operations[owner->value].kind == EvaluationKind::kElidedPure ||
          plan.operations[owner->value].kind == EvaluationKind::kAlias) continue;
      integer = use.purpose == analysis::ReadPurpose::kOperand && IntegerCoercion(op.operation);
    }
    observed[root.value] |= integer ? kIntegerObservation : kOtherObservation;
  }
  return observed;
}
bool Candidate(const analysis::ValueProgram& graph, const EvaluationPlan& plan, std::size_t i) {
  const auto& op = graph.ops[i];
  return op.life == analysis::ValueLife::kLive && op.result && op.result->value < graph.values.size() &&
      plan.operations[i].kind != EvaluationKind::kElidedPure && plan.operations[i].kind != EvaluationKind::kAlias &&
      std::holds_alternative<analysis::ValueCopy>(op.operation) && analysis::CanErasePureOp(op) && op.use_count == 1 &&
      op.first_use.value < graph.uses.size() && ErasableNaN(graph.uses[op.first_use.value].operand);
}
}
std::variant<IntegerNaNCost, EvaluationError> BuildIntegerNaNs(const analysis::ValueProgram& graph,
    EvaluationPlan& plan, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Require(plan.operations.size() == graph.ops.size() && plan.integer_nans.empty());
    bool any = false;
    for (std::size_t i=0;i<graph.ops.size();++i) { quota.Step(); any |= Candidate(graph,plan,i); }
    if (!any) return IntegerNaNCost{0,budget.work-quota.left.work};
    quota.Array(plan.integer_nans,graph.ops.size());
    const auto observations = Observations(graph,plan,quota);
    for (std::size_t i=0;i<graph.ops.size();++i) {
      quota.Step();
      if (Candidate(graph,plan,i) && observations[graph.ops[i].result->value] == kIntegerObservation)
        plan.integer_nans[i] = NaNProjection::kIntegerCoercion;
    }
    return IntegerNaNCost{budget.bytes-quota.left.bytes,budget.work-quota.left.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, EvaluationError> VerifyIntegerNaNs(const analysis::ValueProgram& graph,
    const EvaluationPlan& plan, analysis::SemanticBudget budget) {
  try {
    if (plan.integer_nans.empty()) return std::uint64_t{0};
    Quota quota(budget); Require(plan.integer_nans.size() == graph.ops.size());
    const auto observations = Observations(graph,plan,quota);
    for (std::size_t i=0;i<graph.ops.size();++i) {
      quota.Step();
      if (plan.integer_nans[i] == NaNProjection::kExact) continue;
      Require(plan.integer_nans[i] == NaNProjection::kIntegerCoercion && Candidate(graph,plan,i) &&
          observations[graph.ops[i].result->value] == kIntegerObservation);
    }
    return budget.work-quota.left.work;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
