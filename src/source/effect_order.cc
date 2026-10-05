#include "effect_order.h"
#include "cell_evaluation.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
bool HasOrderedEffect(const analysis::ValueOp& op) {
  using namespace analysis;
  return op.life == ValueLife::kLive && (op.normal != LanguageEffect::kNone ||
      op.before_throw != LanguageEffect::kNone || op.exceptional != ExceptionalCompletion::kImpossible ||
      IsEnvironmentState(op.operation) || std::holds_alternative<ValueDebugger>(op.operation));
}
bool EffectOrder::Before(const analysis::ValueProgram& graph, analysis::OpId id,
    analysis::WorkBlockId block, std::uint32_t order) const {
  const auto& op = graph.ops[id.value];
  if (op.block == block) return op.order < order;
  const auto completed = completions.empty() ? std::optional{op.block} : completions[id.value];
  return completed && dominators.Dominates({completed->value}, {block.value});
}
std::variant<EffectOrder, analysis::ValueAvailabilityError> BuildEffectOrder(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ValueAvailabilityError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok) { if (!ok) throw Error::kInvalidGraph; };
    const auto step = [&](std::uint64_t n = 1) {
      if (n > budget.work) throw Error::kWorkLimit;
      budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t width) {
      if (n > budget.bytes / width) throw Error::kResourceLimit;
      budget.bytes -= n * width;
    };
    const auto size = [&]<class T>(std::vector<T>& v, std::size_t n) {
      admit(n, sizeof(T)); v.resize(n); admit(v.capacity() - n, sizeof(T));
    };
    EffectOrder result;
    const bool phased = !graph.phases.empty();
    if (phased) {
      auto verified = VerifyPhasedValueFlow(graph, budget);
      if (const auto* error = std::get_if<Error>(&verified)) return *error;
      auto& flow = std::get<PhasedValueFlowProof>(verified);
      step(flow.work); admit(flow.allocated_bytes, 1);
      result.dominators = std::move(flow.dominators);
      size(result.completions, graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i) {
        step(); const auto& op = graph.ops[i]; const auto& phase = graph.phases[op.block.value];
        if (phase.phase != ExceptionPhase::kBefore || phase.operation != OpId{static_cast<std::uint32_t>(i)}) {
          result.completions[i] = op.block; continue;
        }
        const auto& block = graph.blocks[op.block.value];
        for (std::uint32_t e = 0; e < block.edge_count; ++e) {
          step(); const auto& edge = graph.edges[block.first_edge.value + e];
          if (edge.kind == FlowEdgeKind::kException) continue;
          const auto& target = graph.phases[edge.target.value];
          require(!result.completions[i] && target.phase == ExceptionPhase::kCommit && target.operation == phase.operation);
          result.completions[i] = edge.target;
        }
      }
    } else {
      auto verified = VerifyValueFlow(graph, budget);
      if (const auto* error = std::get_if<Error>(&verified)) return *error;
      auto& flow = std::get<ValueFlowProof>(verified);
      step(flow.work); admit(flow.allocated_bytes, 1);
      result.dominators = std::move(flow.dominators);
    }
    const auto count = graph.blocks.size(); require(count <= UINT32_MAX && graph.ops.size() <= UINT32_MAX);
    std::vector<std::optional<OpId>> tails;
    std::vector<std::size_t> offsets, cursor, incoming, finish, stack, next;
    std::vector<std::uint8_t> visited;
    std::vector<std::optional<std::uint32_t>> components;
    size(tails, count); size(offsets, count + 1); size(cursor, count);
    size(finish, count); size(stack, count); size(next, count); size(visited, count); size(components, count);
    size(result.before, graph.ops.size()); size(result.position, graph.ops.size());
    size(result.after, graph.ops.size());
    size(result.normal_return, graph.ops.size());
    std::vector<std::size_t> successors; size(successors, count);
    for (std::size_t b = 0; b < count; ++b) {
      step(); const auto& block = graph.blocks[b];
      for (std::uint32_t p = 0; p < block.op_count; ++p) {
        step(); const OpId id{block.first_op.value + p};
        if (HasOrderedEffect(graph.ops[id.value])) tails[b] = id;
      }
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        step(); const auto& edge = graph.edges[block.first_edge.value + e];
        if (edge.kind == FlowEdgeKind::kException) continue;
        ++offsets[edge.target.value + 1]; ++successors[b];
      }
    }
    for (std::size_t b = 0; b < count; ++b) { step(); offsets[b + 1] += offsets[b]; cursor[b] = offsets[b]; }
    size(incoming, graph.edges.size());
    for (const auto& edge : graph.edges) {
      step(); if (edge.kind != FlowEdgeKind::kException) incoming[cursor[edge.target.value]++] = edge.source.value;
    }
    // Serialize each normal component in reverse postorder. Catch entries are
    // separate roots; this ordering never proves cross-phase effect movement.
    const auto traverse = [&](bool pure_dependencies) {
      std::size_t finished = 0;
      std::fill(visited.begin(), visited.end(), 0); step(count);
      for (std::size_t root = 0; root < count; ++root) {
        step(); if (visited[root]) continue;
        std::size_t depth = 1; stack[0] = root; next[0] = 0; visited[root] = 1;
        while (depth) {
          step(); const auto b = stack[depth - 1]; const auto& block = graph.blocks[b];
          if ((!pure_dependencies || !tails[b]) && next[depth - 1] < block.edge_count) {
            const auto& edge = graph.edges[block.first_edge.value + next[depth - 1]++];
            if (edge.kind == FlowEdgeKind::kException) continue;
            const auto target = edge.target.value;
            if (!visited[target]) { visited[target] = 1; stack[depth] = target; next[depth++] = 0; }
          } else { finish[finished++] = b; --depth; }
        }
      }
      require(finished == count);
    };
    traverse(false);
    std::uint32_t position = 0;
    for (std::size_t n = count; n; --n) {
      step(); const auto& block = graph.blocks[finish[n - 1]];
      for (std::uint32_t p = 0; p < block.op_count; ++p) { step(); result.position[block.first_op.value + p] = position++; }
    }
    traverse(true);
    std::uint32_t component_count = 0;
    for (std::size_t n = count; n; --n) {
      step(); const auto root = finish[n - 1]; if (components[root]) continue;
      std::size_t depth = 1; stack[0] = root; components[root] = component_count;
      while (depth) {
        step(); const auto b = stack[--depth];
        for (auto p = offsets[b]; p < offsets[b + 1]; ++p) {
          step(); const auto source = incoming[p];
          if (tails[source] || components[source]) continue;
          components[source] = component_count; stack[depth++] = source;
        }
      }
      ++component_count;
    }
    std::vector<std::size_t> members, member_offsets;
    size(member_offsets, component_count + 1); size(members, count);
    for (const auto id : components) { step(); ++member_offsets[*id + 1]; }
    for (std::size_t c = 0; c < component_count; ++c) { step(); member_offsets[c + 1] += member_offsets[c]; cursor[c] = member_offsets[c]; }
    for (std::size_t b = 0; b < count; ++b) { step(); members[cursor[*components[b]]++] = b; }
    std::vector<EffectPredecessor> entries; size(entries, component_count);
    for (std::uint32_t c = 0; c < component_count; ++c) {
      step(); std::optional<EffectPredecessor> unique; bool differs = false;
      const auto merge = [&](EffectPredecessor input) { if (!unique) unique = input; else if (*unique != input) differs = true; };
      for (auto m = member_offsets[c]; m < member_offsets[c + 1]; ++m) {
        step(); const auto b = members[m];
        // Method and catch entries start an effect history even when a normal
        // backedge also reaches them. Root identity does not come from indegree.
        if (!b || (phased && graph.phases[b].phase == ExceptionPhase::kCatch) ||
            offsets[b] == offsets[b + 1]) merge(EntryEffect{});
        for (auto p = offsets[b]; p < offsets[b + 1]; ++p) {
          step(); const auto source = incoming[p];
          if (tails[source]) merge(*tails[source]);
          else if (*components[source] != c) {
            // Kosaraju emits condensation components in dependency order.
            require(*components[source] < c); merge(entries[*components[source]]);
          }
        }
      }
      require(unique.has_value());
      entries[c] = differs ? EffectPredecessor{EffectJoin{{static_cast<std::uint32_t>(members[member_offsets[c]])}}} : *unique;
    }
    for (std::size_t b = 0; b < count; ++b) {
      step(); auto previous = entries[*components[b]]; const auto& block = graph.blocks[b];
      for (std::uint32_t p = 0; p < block.op_count; ++p) {
        step(); const OpId id{block.first_op.value + p}; result.before[id.value] = previous;
        if (HasOrderedEffect(graph.ops[id.value])) previous = id;
      }
    }
    // Backwards must-reach equations stop at each block's first real effect.
    // Every successor must agree on one endpoint. Real normal returns agree on
    // completion; unresolved pure cycles fail because termination cannot be
    // inferred from a CFG SCC. Return operands remain separately checked.
    // This complements before[] (past identity), rather than comparing counts.
    struct NormalReturn { bool operator==(const NormalReturn&) const = default; };
    using Endpoint = std::variant<std::monostate, OpId, NormalReturn>;
    std::vector<std::optional<OpId>> heads;
    std::vector<Endpoint> endpoints;
    std::vector<std::size_t> pending;
    std::vector<std::uint8_t> failed, queued;
    size(heads, count); size(endpoints, count); size(pending, count);
    size(failed, count); size(queued, count);
    std::size_t queue_size = 0;
    for (std::size_t b = 0; b < count; ++b) {
      step(); const auto& block = graph.blocks[b];
      for (std::uint32_t p = 0; p < block.op_count; ++p) {
        step(); const OpId id{block.first_op.value + p};
        const auto& op = graph.ops[id.value];
        if (HasOrderedEffect(op)) { heads[b] = id; break; }
        // Semantic return ends normal source execution. Phased IR may still
        // retain its valueless Commit; that synthetic successor is not a
        // missing source return or permission to execute another language event.
        if (op.life == ValueLife::kLive && std::holds_alternative<ValueReturn>(op.operation)) {
          endpoints[b] = NormalReturn{}; break;
        }
      }
      if (heads[b] || std::holds_alternative<NormalReturn>(endpoints[b]) || !successors[b]) {
        if (heads[b]) endpoints[b] = *heads[b];
        else if (!std::holds_alternative<NormalReturn>(endpoints[b])) for (auto p = block.op_count; p; --p) {
          step(); const auto& op = graph.ops[block.first_op.value+p-1];
          if (op.life != ValueLife::kLive) continue;
          if (std::holds_alternative<ValueReturn>(op.operation)) endpoints[b] = NormalReturn{};
          break;
        }
        queued[b] = 1; stack[queue_size++] = b;
      } else pending[b] = successors[b];
    }
    for (std::size_t first = 0; first < queue_size; ++first) {
      step(); const auto b = stack[first];
      for (auto p = offsets[b]; p < offsets[b + 1]; ++p) {
        step(); const auto parent = incoming[p]; if (queued[parent]) continue;
        if (std::holds_alternative<std::monostate>(endpoints[b]) ||
            (!std::holds_alternative<std::monostate>(endpoints[parent]) && endpoints[parent] != endpoints[b])) failed[parent] = 1;
        else endpoints[parent] = endpoints[b];
        require(pending[parent] != 0); --pending[parent];
        if (!pending[parent] || failed[parent]) {
          if (failed[parent]) endpoints[parent] = std::monostate{};
          queued[parent] = 1; stack[queue_size++] = parent;
        }
      }
    }
    if (queued[0]) if (const auto* first = std::get_if<OpId>(&endpoints[0])) result.first = *first;
    for (std::size_t b = 0; b < count; ++b) {
      step(); const auto& block = graph.blocks[b];
      Endpoint following; bool agrees = successors[b] != 0;
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        step(); const auto& edge = graph.edges[block.first_edge.value + e];
        if (edge.kind == FlowEdgeKind::kException) continue;
        const auto target = edge.target.value;
        if (!queued[target] || std::holds_alternative<std::monostate>(endpoints[target]) ||
            (!std::holds_alternative<std::monostate>(following) && following != endpoints[target])) agrees = false;
        else following = endpoints[target];
      }
      if (!agrees) following = std::monostate{};
      for (auto p = block.op_count; p; --p) {
        step(); const OpId id{block.first_op.value + p - 1};
        if (const auto* next_effect = std::get_if<OpId>(&following)) result.after[id.value] = *next_effect;
        result.normal_return[id.value] = std::holds_alternative<NormalReturn>(following);
        const auto& op = graph.ops[id.value];
        if (HasOrderedEffect(op)) following = id;
        else if (op.life == ValueLife::kLive && std::holds_alternative<ValueReturn>(op.operation)) following = NormalReturn{};
      }
    }
    result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
