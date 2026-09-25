#include "value_flow.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/analysis/dominators.h"

namespace abcd::analysis {
namespace {
using Error = ValueAvailabilityError;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
class FlowQuota {
 public:
  explicit FlowQuota(SemanticBudget budget) : remaining_(budget) {}
  void Work(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit);
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <typename T> void Reserve(std::vector<T>& items, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T));
    items.reserve(count);
    if (items.capacity() > count) Admit(items.capacity() - count, sizeof(T));
  }
  SemanticBudget Remaining() const { return remaining_; }
 private:
  SemanticBudget remaining_;
};
struct PhiKey {
  ValueId phi;
  std::optional<EdgeId> edge;  // Absence is MethodEntry, not a fabricated edge.
  auto operator<=>(const PhiKey&) const = default;
};

NormalFlow ReadFlow(const ValueProgram& graph, FlowQuota& quota,
                    std::vector<std::size_t>& predecessors) {
  NormalFlow flow;
  Require(!graph.blocks.empty(), Error::kInvalidGraph);
  quota.Reserve(flow.blocks, graph.blocks.size());
  quota.Reserve(predecessors, graph.blocks.size());
  quota.Work(graph.blocks.size());
  predecessors.resize(graph.blocks.size());
  std::size_t next_op = 0;
  std::size_t next_edge = 0;
  for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
    quota.Work();
    const auto& block = graph.blocks[b];
    Require(block.first_op.value == next_op &&
        block.op_count <= graph.ops.size() - next_op &&
        block.first_edge.value == next_edge &&
        block.edge_count <= graph.edges.size() - next_edge &&
        block.edge_count <= kMaxNormalSuccessors, Error::kInvalidGraph);
    FlowBlock adapted;
    std::optional<OpId> last;
    for (std::uint32_t order = 0; order < block.op_count; ++order) {
      quota.Work();
      const auto index = next_op + order;
      const auto& op = graph.ops[index];
      Require(op.block.value == b && op.order == order, Error::kInvalidGraph);
      if (op.life != ValueLife::kLive) continue;
      if (last) {
        const auto& prior = graph.ops[last->value];
        Require(!std::holds_alternative<ValueReturn>(prior.operation) &&
                !std::holds_alternative<ValueJump>(prior.operation) &&
                !std::holds_alternative<ValueBranch>(prior.operation) &&
                prior.exceptional != ExceptionalCompletion::kAlwaysThrow,
                Error::kInvalidTerminator);
      }
      last = OpId{static_cast<std::uint32_t>(index)};
    }
    next_op += block.op_count;
    for (std::uint8_t e = 0; e < block.edge_count; ++e) {
      quota.Work();
      const auto& edge = graph.edges[next_edge++];
      Require(edge.source.value == b && edge.successor == e &&
              edge.target.value < graph.blocks.size(), Error::kInvalidGraph);
      Require(edge.kind != FlowEdgeKind::kResume, Error::kResumeFlow);
      Require(edge.kind == FlowEdgeKind::kTaken || edge.kind == FlowEdgeKind::kFallthrough,
              Error::kInvalidGraph);
      adapted.successors[adapted.successor_count++] = {{edge.target.value}, edge.kind};
      ++predecessors[edge.target.value];
    }
    const auto* terminal = last ? &graph.ops[last->value] : nullptr;
    const bool returns = terminal && std::holds_alternative<ValueReturn>(terminal->operation);
    const bool jumps = terminal && std::holds_alternative<ValueJump>(terminal->operation);
    const bool branches = terminal && std::holds_alternative<ValueBranch>(terminal->operation);
    const bool throws = terminal && terminal->exceptional == ExceptionalCompletion::kAlwaysThrow;
    if (returns || throws) {
      Require(block.edge_count == 0, Error::kInvalidTerminator);
    } else if (branches) {
      Require(block.edge_count == kMaxNormalSuccessors &&
          adapted.successors[0].kind == FlowEdgeKind::kTaken &&
          adapted.successors[1].kind == FlowEdgeKind::kFallthrough,
          Error::kInvalidTerminator);
    } else {
      Require(block.edge_count == 1 && adapted.successors[0].kind ==
          (jumps ? FlowEdgeKind::kTaken : FlowEdgeKind::kFallthrough),
          Error::kInvalidTerminator);
    }
    flow.blocks.push_back(adapted);
  }
  Require(next_op == graph.ops.size() && next_edge == graph.edges.size(), Error::kInvalidGraph);
  return flow;
}

bool Available(const ValueProgram& graph, const DominatorTree& dominators,
               ValueId value, WorkBlockId block, std::uint32_t order) {
  const auto& definition = graph.values[value.value];
  if (std::holds_alternative<EntryDefinition>(definition.origin)) return true;
  if (const auto* result = std::get_if<OpResult>(&definition.origin)) {
    const auto& op = graph.ops[result->op.value];
    if (op.exceptional == ExceptionalCompletion::kAlwaysThrow) return false;
    if (op.block == block) return op.order < order;
    return dominators.Dominates({op.block.value}, {block.value});
  }
  const auto phi = std::get<PhiDefinition>(definition.origin);
  return dominators.Dominates({phi.block.value}, {block.value});
}
}  // namespace

std::variant<ValueFlowProof, ValueAvailabilityError> VerifyValueFlow(
    const ValueProgram& graph, SemanticBudget budget) {
  try {
    FlowQuota quota{budget};
    quota.Admit(graph.uses.size(), sizeof(std::uint8_t));
    quota.Work(graph.values.size());
    quota.Work(graph.ops.size());
    quota.Work(graph.uses.size());
    quota.Work(graph.uses.size());
    quota.Work(graph.uses.size());
    if (const auto error = VerifyValueUses(graph, graph.uses.size()))
      throw *error == ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kInvalidGraph;
    std::vector<std::size_t> predecessors;
    auto flow = ReadFlow(graph, quota, predecessors);
    const auto allowance = quota.Remaining();
    auto built = BuildNormalDominators(flow, {}, {allowance.bytes, allowance.work});
    const auto* dominators = std::get_if<DominatorTree>(&built);
    if (!dominators) {
      const auto error = std::get<DominatorError>(built);
      if (error == DominatorError::kWorkLimit) return Error::kWorkLimit;
      if (error == DominatorError::kResourceLimit) return Error::kResourceLimit;
      return Error::kInvalidGraph;
    }
    quota.Work(dominators->work.steps);
    // Conservatively retain the builder's peak allocation charge, although its
    // temporary arrays have already been released. No independent full budget.
    quota.Admit(dominators->work.allocated_bytes, 1);
    for (const auto& node : dominators->nodes) {
      quota.Work();
      Require(node.begin != 0, Error::kUnreachableBlock);
    }
    std::vector<PhiKey> keys;
    std::vector<std::size_t> counts;
    quota.Reserve(keys, graph.uses.size());
    quota.Reserve(counts, graph.values.size());
    quota.Work(graph.values.size());
    counts.resize(graph.values.size());
    for (const auto& use : graph.uses) {
      quota.Work();
      if (use.life != ValueLife::kLive) continue;
      const auto* value = std::get_if<ValueId>(&use.operand);
      if (!value) continue;
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        const auto& op = graph.ops[owner->value];
        Require(Available(graph, *dominators, *value, op.block, op.order),
                Error::kUnavailableValue);
      } else {
        const auto phi_owner = std::get<ValueId>(use.owner);
        const auto phi = std::get<PhiDefinition>(graph.values[phi_owner.value].origin);
        ++counts[phi_owner.value];
        if (const auto* edge = std::get_if<EdgeId>(&*use.incoming)) {
          Require(edge->value < graph.edges.size(), Error::kInvalidPhi);
          const auto& incoming = graph.edges[edge->value];
          Require(incoming.target == phi.block, Error::kInvalidPhi);
          Require(Available(graph, *dominators, *value, incoming.source,
                            graph.blocks[incoming.source.value].op_count),
                  Error::kUnavailableValue);
          keys.push_back({phi_owner, *edge});
        } else {
          Require(phi.block.value == 0, Error::kInvalidPhi);
          Require(std::holds_alternative<EntryDefinition>(graph.values[value->value].origin),
                  Error::kUnavailableValue);
          keys.push_back({phi_owner, {}});
        }
      }
    }
    // Sorting sparse occurrence keys detects duplicate parallel-edge inputs
    // without a dense values-by-edges table or one CFG scan per phi.
    std::sort(keys.begin(), keys.end(), [&](const PhiKey& a, const PhiKey& b) {
      quota.Work();
      return a < b;
    });
    for (std::size_t i = 1; i < keys.size(); ++i) {
      quota.Work();
      Require(keys[i - 1] != keys[i], Error::kInvalidPhi);
    }
    for (std::size_t i = 0; i < graph.values.size(); ++i) {
      quota.Work();
      const auto& value = graph.values[i];
      const auto* phi = std::get_if<PhiDefinition>(&value.origin);
      if (!phi || value.life != ValueLife::kLive) continue;
      const auto expected = predecessors[phi->block.value] + (phi->block.value == 0);
      Require(counts[i] == expected && expected != 0, Error::kInvalidPhi);
    }
    return ValueFlowProof{graph.revision, graph.cfg_revision,
                          budget.work - quota.Remaining().work};
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
