#include "hole_protocol.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = HoleProtocolError;
void Require(bool value, Error error = Error::kInput) {
  if (!value) throw error;
}
enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
struct Guard {
  ValueId payload;
  OpId comparison;
};
struct Predecessor {
  std::optional<EdgeId> edge;
  bool multiple = false;
};
bool Transport(ValueKinds kinds) {
  return !kinds.IsEmpty() && !kinds.Contains(VmValueKind::kEnvironment) &&
         !kinds.Contains(VmValueKind::kOtherInternal);
}
}  // namespace

std::variant<HoleProtocol, HoleProtocolError> AnalyzeHoleProtocol(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  const auto initial = budget;
  try {
    Require(!graph.blocks.empty() &&
            (graph.phases.empty() || graph.phases.size() == graph.blocks.size()));
    Require(graph.values.size() <= UINT32_MAX &&
                graph.ops.size() <= UINT32_MAX &&
                graph.blocks.size() <= UINT32_MAX &&
                graph.edges.size() <= UINT32_MAX,
            Error::kResourceLimit);
    const auto step = [&](std::size_t count = 1) {
      Require(count <= budget.work, Error::kWorkLimit);
      budget.work -= count;
    };
    const auto array = [&]<class T>(std::vector<T>& values, std::size_t count) {
      Require(count <= budget.bytes / sizeof(T), Error::kResourceLimit);
      budget.bytes -= count * sizeof(T);
      values.resize(count);
    };
    Require(graph.uses.size() <= budget.bytes, Error::kResourceLimit);
    budget.bytes -= graph.uses.size();
    step(graph.values.size());
    step(graph.ops.size());
    step(graph.uses.size());
    Require(!VerifyValueUses(graph, graph.uses.size()));
    std::vector<ValueId> roots;
    std::vector<Visit> visits;
    std::vector<std::uint32_t> pending;
    array(roots, graph.values.size());
    array(visits, graph.values.size());
    array(pending, std::max(graph.values.size(), graph.blocks.size()));
    for (std::uint32_t v = 0; v < graph.values.size(); ++v) {
      step();
      roots[v] = {v};
      const auto& value = graph.values[v];
      if (value.first_use && value.domain.Contains(VmValueKind::kHole)) {
        Require(Transport(value.domain), Error::kEscape);
        if (const auto* produced = std::get_if<OpResult>(&value.origin)) {
          Require(produced->op.value < graph.ops.size());
          const auto& operation = graph.ops[produced->op.value].operation;
          Require(std::holds_alternative<ValueHole>(operation) ||
                      std::holds_alternative<ValueCopy>(operation),
                  Error::kEscape);
        } else
          Require(std::holds_alternative<PhiDefinition>(value.origin),
                  Error::kEscape);
      }
      if (const auto* result = std::get_if<OpResult>(&graph.values[v].origin)) {
        Require(result->op.value < graph.ops.size());
        const auto& op = graph.ops[result->op.value];
        if (!std::holds_alternative<ValueCopy>(op.operation)) continue;
        Require(op.use_count == 1 && op.first_use.value < graph.uses.size());
        const auto* source =
            std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
        if (!source) continue;
        Require(source->value < roots.size() &&
                op.normal == LanguageEffect::kNone &&
                op.before_throw == LanguageEffect::kNone &&
                op.exceptional == ExceptionalCompletion::kImpossible);
        roots[v] = *source;
      }
    }
    // Identity tracing differs from inlining: catch and phi copies remain in
    // the AST, but their payload identity is shared by the guard and rethrow.
    for (std::uint32_t v = 0; v < roots.size(); ++v) {
      step();
      if (visits[v] == Visit::kDone) continue;
      std::size_t size = 0;
      auto cursor = v;
      while (visits[cursor] == Visit::kUnseen &&
             roots[cursor].value != cursor) {
        step();
        visits[cursor] = Visit::kActive;
        pending[size++] = cursor;
        cursor = roots[cursor].value;
      }
      Require(visits[cursor] != Visit::kActive);
      visits[cursor] = Visit::kDone;
      while (size) {
        step();
        const auto id = pending[--size];
        roots[id] = roots[cursor];
        visits[id] = Visit::kDone;
      }
    }
    const auto operand = [&](const ValueOp& op,
                             std::uint32_t ordinal) -> ValueId {
      Require(ordinal < op.use_count &&
              op.first_use.value < graph.uses.size() &&
              op.use_count <= graph.uses.size() - op.first_use.value);
      const auto* id = std::get_if<ValueId>(
          &graph.uses[op.first_use.value + ordinal].operand);
      Require(id && id->value < roots.size());
      return roots[id->value];
    };
    HoleProtocol result;
    const auto reserve = [&]<class T>(std::vector<T>& values,
                                      std::size_t count) {
      Require(count <= budget.bytes / sizeof(T), Error::kResourceLimit);
      budget.bytes -= count * sizeof(T);
      values.reserve(count);
    };
    reserve(result.comparisons, graph.ops.size());
    reserve(result.rethrows, graph.ops.size());
    std::vector<std::optional<std::size_t>> comparisons;
    array(comparisons, graph.ops.size());
    for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
      step();
      const auto& op = graph.ops[i];
      Require(op.first_use.value <= graph.uses.size() &&
              op.use_count <= graph.uses.size() - op.first_use.value);
      const auto* binary = std::get_if<ValueBinary>(&op.operation);
      if (!binary || op.use_count != 2) continue;
      const bool equal = binary->op == BinaryOperator::kEqual ||
                         binary->op == BinaryOperator::kStrictEqual;
      if (!equal && binary->op != BinaryOperator::kNotEqual &&
          binary->op != BinaryOperator::kStrictNotEqual)
        continue;
      const auto* left =
          std::get_if<ValueId>(&graph.uses[op.first_use.value].operand);
      if (!left || graph.values[left->value].domain != ValueKinds::Hole())
        continue;
      Require(!graph.phases.empty(), Error::kEscape);
      const auto payload = operand(op, 1);
      Require(Transport(graph.values[payload.value].domain), Error::kEscape);
      // Pinned interpreter NOTEQ reads vreg as left, acc as right. With Hole
      // on the left the slow path cannot invoke user coercion; this is an
      // identity test, not arbitrary JS loose equality on a private token.
      comparisons[i] = result.comparisons.size();
      result.comparisons.push_back({{i}, payload, equal});
    }
    std::vector<Predecessor> predecessors;
    std::vector<std::optional<Guard>> guards;
    array(predecessors, graph.blocks.size());
    array(guards, graph.blocks.size());
    visits.clear();
    array(visits, graph.blocks.size());
    for (std::uint32_t e = 0; e < graph.edges.size(); ++e) {
      step();
      const auto& edge = graph.edges[e];
      Require(edge.source.value < graph.blocks.size() &&
              edge.target.value < predecessors.size());
      auto& target = predecessors[edge.target.value];
      if (target.edge)
        target.multiple = true;
      else
        target.edge = EdgeId{e};
    }
    // A single-parent chain has at most one nearest conditional boundary.
    // Memoize it once; no per-throw backward graph traversal or V*B fact map.
    for (std::uint32_t b = 0; !graph.phases.empty() && b < graph.blocks.size(); ++b) {
      step();
      if (visits[b] == Visit::kDone) continue;
      std::size_t size = 0;
      auto cursor = b;
      std::optional<Guard> guard;
      while (visits[cursor] == Visit::kUnseen) {
        step();
        visits[cursor] = Visit::kActive;
        pending[size++] = cursor;
        const auto& pred = predecessors[cursor];
        if (!pred.edge || pred.multiple || cursor == 0) break;
        const auto& edge = graph.edges[pred.edge->value];
        if (edge.kind == FlowEdgeKind::kException) break;
        const auto source = edge.source.value;
        const auto& phase = graph.phases[source];
        if (phase.phase == ExceptionPhase::kCommit && phase.operation) {
          Require(phase.operation->value < graph.ops.size());
          const auto& op = graph.ops[phase.operation->value];
          if (const auto* branch = std::get_if<ValueBranch>(&op.operation)) {
            const auto condition = operand(op, 0);
            const auto* producer =
                std::get_if<OpResult>(&graph.values[condition.value].origin);
            if (!producer || !comparisons[producer->op.value]) break;
            const auto& test =
                result.comparisons[*comparisons[producer->op.value]];
            if (branch->predicate != UnaryOperator::kFalseOrZero &&
                branch->predicate != UnaryOperator::kTrueOrNonzeroNumber)
              break;
            const bool truth =
                (edge.kind == FlowEdgeKind::kTaken) ==
                (branch->predicate == UnaryOperator::kTrueOrNonzeroNumber);
            if (truth != test.equal)
              guard = Guard{test.payload, test.operation};
            break;
          }
        }
        cursor = source;
      }
      if (visits[cursor] == Visit::kDone) guard = guards[cursor];
      while (size) {
        step();
        const auto id = pending[--size];
        guards[id] = guard;
        visits[id] = Visit::kDone;
      }
    }
    for (const auto& use : graph.uses) {
      step();
      const auto* value = std::get_if<ValueId>(&use.operand);
      if (!value) continue;
      Require(value->value < graph.values.size());
      const auto kinds = graph.values[value->value].domain;
      if (!kinds.Contains(VmValueKind::kHole)) continue;
      Require(Transport(kinds), Error::kEscape);
      if (const auto* phi = std::get_if<ValueId>(&use.owner)) {
        Require(!graph.phases.empty(), Error::kEscape);
        Require(phi->value < graph.values.size() &&
                std::holds_alternative<PhiDefinition>(
                    graph.values[phi->value].origin));
        continue;
      }
      const auto id = std::get<OpId>(use.owner);
      Require(id.value < graph.ops.size());
      const auto& op = graph.ops[id.value];
      if (std::holds_alternative<ValueCopy>(op.operation) ||
          comparisons[id.value])
        continue;
      if (std::holds_alternative<ValueClassCreation>(op.operation)) {
        Require(kinds == ValueKinds::Hole() && use.role == OperandRole::kHeritage &&
            use.purpose == ReadPurpose::kProtocol && op.use_count == 1, Error::kEscape);
        continue;
      }
      if (std::holds_alternative<ValueThrow>(op.operation)) {
        Require(op.block.value < guards.size());
        const auto& guard = guards[op.block.value];
        Require(guard && guard->payload == roots[value->value],
                Error::kUnguardedThrow);
        result.rethrows.push_back({id, guard->comparison});
        continue;
      }
      throw Error::kEscape;
    }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
