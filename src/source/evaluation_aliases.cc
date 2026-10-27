#include "evaluation_aliases.h"
#include "for_of_evaluation.h"

#include <new>
#include <stdexcept>
#include "../analysis/value_edits.h"
#include "../analysis/value_phi.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = EvaluationError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class AliasQuota {
 public:
  explicit AliasQuota(SemanticBudget limit) : initial_(limit), left_(limit) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= left_.work, Error::kWorkLimit); left_.work -= count;
  }
  void Admit(std::size_t bytes) { Require(bytes <= left_.bytes, Error::kResourceLimit); left_.bytes -= bytes; }
  SemanticBudget Remaining() const { return left_; }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Require(count <= left_.bytes / sizeof(T), Error::kResourceLimit);
    left_.bytes -= count * sizeof(T); Step(count); values.resize(count);
    const auto extra = values.capacity() - count;
    Require(extra <= left_.bytes / sizeof(T), Error::kResourceLimit); left_.bytes -= extra * sizeof(T);
  }
  EvaluationAliasCost Cost() const { return {initial_.bytes - left_.bytes, initial_.work - left_.work}; }
 private:
  SemanticBudget initial_, left_;
};
std::optional<ValueId> AliasInput(const ValueProgram& graph, const ValueOp& op) {
  if (op.life != ValueLife::kLive || !std::holds_alternative<ValueCopy>(op.operation) ||
      !CanErasePureOp(op) || !op.result || op.use_count != 1) return {};
  const auto& use = graph.uses[op.first_use.value];
  const auto* source = std::get_if<ValueId>(&use.operand);
  if (!source || use.role != OperandRole::kInput || use.purpose != ReadPurpose::kOperand) return {};
  const auto& value = graph.values[source->value];
  // Phi targets are mutable during parallel edge copies; catch symbols have
  // handler-local scope. Keep their first read in the exact alias graph; the
  // phase planner may fuse that occurrence after proving scope and the no-write
  // interval. Later copies refer to its immutable SSA result without replay.
  if (const auto* entry = std::get_if<EntryDefinition>(&value.origin)) {
    if (!std::holds_alternative<ParameterEntry>(*entry) && !std::holds_alternative<ReceiverEntry>(*entry)) return {};
  } else if (!std::holds_alternative<OpResult>(value.origin)) return {};
  // A copy carrying stronger certified facts must not be replaced by a root
  // with a broader domain. Do not manufacture a guard/refinement here.
  const auto admitted = graph.values[op.result->value].domain;
  if (value.domain.IsEmpty() || value.domain.Join(admitted) != admitted) return {};
  return *source;
}
bool PayloadTransport(const ValueProgram& graph,const EvaluationPlan& plan,OpId id) {
  if(!plan.for_of || !plan.for_of->completion_ports || !ForOfOperation(plan,id))return false;
  const auto& op=graph.ops[id.value];return op.result && !ForOfValue(plan,*op.result) &&
      std::holds_alternative<ValueCopy>(op.operation) && CanErasePureOp(op) &&
      graph.values[op.result->value].domain.IsLanguage();
}
ValueIdentities IdentityFacts(const ValueProgram& graph, AliasQuota& quota) {
  auto indexed = AnalyzeValueIdentities(graph, quota.Remaining());
  if (const auto* error = std::get_if<ValueSimplifyError>(&indexed))
    throw *error == ValueSimplifyError::kWorkLimit ? Error::kWorkLimit :
        *error == ValueSimplifyError::kResourceLimit ? Error::kResourceLimit : Error::kOwnership;
  auto result = std::get<ValueIdentities>(std::move(indexed));
  quota.Admit(result.allocated_bytes); quota.Step(result.work); return result;
}
enum class AliasVisit : std::uint8_t { kUnseen, kActive, kResolved };
}  // namespace

bool IsIdentityPhiInput(const EvaluationPlan& plan, const analysis::OperandUse& use) {
  const auto* phi = std::get_if<analysis::ValueId>(&use.owner);
  return phi && !plan.aliases.empty() && plan.aliases[phi->value] != *phi;
}
bool SourceIdentityRoot(const analysis::ValueProgram& graph, analysis::ValueId root) {
  using namespace analysis;
  const auto& value = graph.values[root.value];
  if (value.life != ValueLife::kLive || !value.domain.IsLanguage()) return false;
  if (const auto* entry = std::get_if<EntryDefinition>(&value.origin))
    return std::holds_alternative<ParameterEntry>(*entry) || std::holds_alternative<ReceiverEntry>(*entry);
  return std::holds_alternative<OpResult>(value.origin);
}
std::optional<analysis::ValueId> SourcePhiAlias(const analysis::ValueProgram& graph,
    std::span<const analysis::ValueId> identities, analysis::ValueId value) {
  using namespace analysis;
  if (identities.empty() || graph.values[value.value].life != ValueLife::kLive) return {};
  const auto* phi = std::get_if<PhiDefinition>(&graph.values[value.value].origin);
  if (!phi || (!graph.phases.empty() && graph.phases[phi->block.value].phase == ExceptionPhase::kCatch)) return {};
  const auto root = identities[value.value];
  return root != value && SourceIdentityRoot(graph, root) ? std::optional{root} : std::nullopt;
}

std::variant<EvaluationAliasCost, EvaluationFailure> BuildEvaluationAliases(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget) {
  try {
    Require(plan.operations.size() == graph.ops.size() &&
        plan.aliases.empty(), Error::kOwnership);
    AliasQuota quota(budget);
    quota.Array(plan.aliases, graph.values.size());
    std::vector<AliasVisit> visits;
    std::vector<ValueId> pending;
    quota.Array(visits, graph.values.size()); quota.Array(pending, graph.values.size());
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Step(); plan.aliases[i] = {static_cast<std::uint32_t>(i)};
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step();
      if (plan.operations[i].kind == EvaluationKind::kElidedPure || (ForOfOperation(plan,{static_cast<std::uint32_t>(i)}) && !PayloadTransport(graph,plan,{static_cast<std::uint32_t>(i)}))) continue;
      const auto& op = graph.ops[i];
      if (const auto source = AliasInput(graph, op)) {
        Require(*source != *op.result, Error::kOwnership);
        plan.aliases[op.result->value] = *source;
        plan.operations[i].kind = EvaluationKind::kAlias;
      }
    }
    ValueIdentities computed;
    if(!plan.cells)computed=IdentityFacts(graph,quota);
    const auto& identities=plan.cells?plan.cells->identities:computed;
    Require(identities.canonical.size()==graph.values.size(),Error::kOwnership);
    for (std::size_t i=0;i<graph.values.size();++i) {
      quota.Step(); const ValueId value{static_cast<std::uint32_t>(i)};
      if ((plan.for_in && plan.for_in->values[i]) || ForOfValue(plan,{static_cast<std::uint32_t>(i)})) {
        // Closed protocol SCCs have an immutable raw definition, not a source
        // phi location. This identity does not create a key or enumerator symbol.
        plan.aliases[i] = identities.canonical[i]; continue;
      }
      if (const auto root = SourcePhiAlias(graph, identities.canonical, value)) {
        plan.aliases[i] = *root; continue;
      }
      const auto* result = std::get_if<OpResult>(&graph.values[i].origin);
      const auto root = identities.canonical[i];
      if (!result || root == value || !SourceIdentityRoot(graph, root) ||
          (plan.operations[result->op.value].kind == EvaluationKind::kElidedPure || (ForOfOperation(plan,result->op) && !PayloadTransport(graph,plan,result->op)))) continue;
      // The equation index certifies actual pure copies, including first
      // materializations of a collapsed phi. Real mutable phi/catch roots keep
      // their original first snapshot; later ordinary aliases still compress.
      plan.aliases[i] = root;
      if(!std::holds_alternative<ValueCopyProperties>(graph.ops[result->op.value].operation))plan.operations[result->op.value].kind = EvaluationKind::kAlias;
    }
    // Functional-graph compression, as in value_simplify.cc. Each unresolved
    // vertex is visited once; no per-use/per-call walk down a copy chain.
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Step();
      if (visits[i] == AliasVisit::kResolved) continue;
      std::size_t count = 0;
      ValueId cursor{static_cast<std::uint32_t>(i)};
      while (visits[cursor.value] == AliasVisit::kUnseen && plan.aliases[cursor.value] != cursor) {
        quota.Step(); visits[cursor.value] = AliasVisit::kActive;
        pending[count++] = cursor; cursor = plan.aliases[cursor.value];
      }
      Require(visits[cursor.value] != AliasVisit::kActive, Error::kOwnership);
      const auto root = plan.aliases[cursor.value];
      visits[cursor.value] = AliasVisit::kResolved;
      for (std::size_t j = 0; j < count; ++j) {
        quota.Step(); plan.aliases[pending[j].value] = root;
        visits[pending[j].value] = AliasVisit::kResolved;
      }
    }
    return quota.Cost();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<std::uint64_t, EvaluationFailure> VerifyEvaluationAliases(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget) {
  try {
    Require(plan.aliases.size() == graph.values.size(), Error::kOwnership);
    AliasQuota quota(budget);
    const auto identities = IdentityFacts(graph, quota);
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Step();
      const auto root = plan.aliases[i];
      if ((plan.for_in && plan.for_in->values[i]) || ForOfValue(plan,{static_cast<std::uint32_t>(i)})) {
        Require(root == identities.canonical[i], Error::kOwnership);
        continue;
      }
      Require(root.value < graph.values.size() && plan.aliases[root.value] == root, Error::kOwnership);
      const auto* result = std::get_if<OpResult>(&graph.values[i].origin);
      if (std::holds_alternative<PhiDefinition>(graph.values[i].origin)) {
        Require(root == SourcePhiAlias(graph,identities.canonical,ValueId{static_cast<std::uint32_t>(i)}).value_or(ValueId{static_cast<std::uint32_t>(i)}),Error::kOwnership);
        continue;
      }
      if (root.value == i) {
        Require(!result || plan.operations[result->op.value].kind != EvaluationKind::kAlias, Error::kOwnership);
        continue;
      }
      Require(result && (plan.operations[result->op.value].kind == EvaluationKind::kAlias || PayloadTransport(graph,plan,result->op) || std::holds_alternative<ValueCopyProperties>(graph.ops[result->op.value].operation)), Error::kOwnership);
      const auto& op = graph.ops[result->op.value];
      const auto source = AliasInput(graph, op);
      Require(op.result == ValueId{static_cast<std::uint32_t>(i)} &&
          ((source && plan.aliases[source->value] == root) ||
           (identities.canonical[i] == root && SourceIdentityRoot(graph, root))), Error::kOwnership);
    }
    return quota.Cost().work;
  } catch (Error error) { return error; }
}
}  // namespace abcd::source
