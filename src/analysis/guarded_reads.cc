#include "guarded_reads.h"
#include "value_phi.h"
#include "idf_scratch.h"
#include "abcd/analysis/dominators.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::analysis {
namespace {
using Error = GuardedReadError;
// Bottom -> one event -> conflicting paths. Each block changes at most twice;
// an event's output is constant. Loops converge without a per-cell CFG walk.
enum class StateKind : std::uint8_t { kBottom, kEvent, kConflict };
struct State {
  StateKind kind = StateKind::kBottom;
  OpId event;
  bool operator==(const State&) const = default;
};
State Join(State a, State b) {
  if (a.kind == StateKind::kBottom) return b;
  if (b.kind == StateKind::kBottom || a == b) return a;
  return {StateKind::kConflict, {}};
}
bool Event(const ValueOp& op) {
  return op.life == ValueLife::kLive && (op.normal != LanguageEffect::kNone ||
      op.before_throw != LanguageEffect::kNone || op.exceptional != ExceptionalCompletion::kImpossible);
}
}  // namespace
std::variant<GuardedReads, GuardedReadError> ProveGuardedReads(
    const ValueProgram& graph, SemanticBudget budget) {
  try {
    auto remaining = budget;
    const auto step = [&](std::uint64_t count = 1) {
      if (count > remaining.work) throw Error::kWorkLimit;
      remaining.work -= count;
    };
    if (graph.blocks.empty() || graph.phases.size() != graph.blocks.size() ||
        graph.blocks.size() >= UINT32_MAX || graph.ops.size() >= UINT32_MAX) return Error::kInput;
    detail::IdfScratch memory{remaining.bytes};
    auto identities = AnalyzeValueIdentities(graph, {memory.Available(), remaining.work});
    if (const auto* error = std::get_if<ValueSimplifyError>(&identities)) {
      if (*error == ValueSimplifyError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ValueSimplifyError::kWorkLimit) return Error::kWorkLimit;
      return Error::kIdentity;
    }
    auto identity = std::get<ValueIdentities>(std::move(identities));
    memory.Retain(identity.allocated_bytes); step(identity.work);
    std::pmr::vector<std::uint32_t> offsets(graph.blocks.size() + 1, 0, &memory);
    std::pmr::vector<BlockId> targets(graph.edges.size(), {}, &memory);
    for (const auto& edge : graph.edges) {
      step();
      if (edge.source.value >= graph.blocks.size() || edge.target.value >= graph.blocks.size()) return Error::kInput;
      ++offsets[edge.source.value + 1];
    }
    for (std::size_t i = 1; i < offsets.size(); ++i) { step(); offsets[i] += offsets[i - 1]; }
    std::pmr::vector<std::uint32_t> next(offsets, &memory);
    for (const auto& edge : graph.edges) { step(); targets[next[edge.source.value]++] = BlockId{edge.target.value}; }
    auto dominance = BuildDominators({offsets, targets}, {}, {memory.Available(), remaining.work});
    if (const auto* error = std::get_if<DominatorError>(&dominance)) {
      if (*error == DominatorError::kResourceLimit) return Error::kResourceLimit;
      if (*error == DominatorError::kWorkLimit) return Error::kWorkLimit;
      return Error::kInput;
    }
    auto dom = std::get<DominatorTree>(std::move(dominance));
    memory.Retain(dom.work.allocated_bytes); step(dom.work.steps);
    std::pmr::vector<std::optional<std::size_t>> exits(graph.ops.size(), {}, &memory);
    for (std::size_t i = 0; i < graph.exceptional_exits.size(); ++i) {
      step(); const auto& exit = graph.exceptional_exits[i];
      if (exit.op.value >= graph.ops.size() || exits[exit.op.value]) return Error::kInput;
      exits[exit.op.value] = i;
    }
    std::pmr::vector<State> incoming(graph.blocks.size(), {}, &memory);
    std::pmr::vector<std::optional<OpId>> last(graph.blocks.size(), {}, &memory);
    std::pmr::vector<std::optional<WorkBlockId>> commits(graph.ops.size(), {}, &memory);
    std::pmr::vector<WorkBlockId> queue(&memory);
    if (graph.blocks.size() > (std::numeric_limits<std::size_t>::max() - 1) / 2) return Error::kResourceLimit;
    queue.reserve(2 * graph.blocks.size() + 1);
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      step(); const auto& block = graph.blocks[b];
      if (block.first_op.value > graph.ops.size() || block.op_count > graph.ops.size() - block.first_op.value) return Error::kInput;
      for (std::uint32_t n = 0; n < block.op_count; ++n) {
        step(); const OpId id{block.first_op.value + n};
        if (graph.ops[id.value].block.value != b) return Error::kInput;
        if (Event(graph.ops[id.value])) last[b] = id;
      }
      const auto& phase = graph.phases[b];
      if (phase.phase == ExceptionPhase::kCommit && phase.operation) {
        if (phase.operation->value >= commits.size() || commits[phase.operation->value]) return Error::kInput;
        commits[phase.operation->value] = WorkBlockId{static_cast<std::uint32_t>(b)};
      }
      if (dom.nodes[b].begin && (b == 0 || phase.phase == ExceptionPhase::kCatch || last[b])) {
        // Event blocks may propagate before their inputs arrive: their output
        // is fixed. Catch entry is a new exception activation, never a bridge.
        if (b == 0 || phase.phase == ExceptionPhase::kCatch) incoming[b] = {StateKind::kConflict, {}};
        queue.push_back({static_cast<std::uint32_t>(b)});
      }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
      step(); const auto b = queue[head].value;
      const auto output = last[b] ? State{StateKind::kEvent, *last[b]} : incoming[b];
      const auto& block = graph.blocks[b];
      for (std::size_t e = block.first_edge.value; e < std::size_t(block.first_edge.value) + block.edge_count; ++e) {
        step(); const auto& edge = graph.edges[e];
        if (edge.kind == FlowEdgeKind::kException || graph.phases[edge.target.value].phase == ExceptionPhase::kCatch) continue;
        auto& target = incoming[edge.target.value]; const auto joined = Join(target, output);
        if (joined == target) continue;
        target = joined;
        // Event blocks already propagated their constant output once.
        if (!last[edge.target.value]) queue.push_back(edge.target);
      }
    }
    std::pmr::vector<std::optional<GuardedRead>> candidates(graph.ops.size(), {}, &memory);
    std::pmr::vector<std::uint8_t> invalid(graph.ops.size(), 0, &memory);
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      step(); if (!dom.nodes[b].begin) continue;
      auto prior = incoming[b]; const auto& block = graph.blocks[b];
      for (std::uint32_t n = 0; n < block.op_count; ++n) {
        step(); const OpId id{block.first_op.value + n}; const auto& op = graph.ops[id.value];
        if (op.life != ValueLife::kLive) continue;
        if (std::holds_alternative<ValueGuard>(op.operation) && std::get<ValueGuard>(op.operation).name && op.use_count == 1 && !op.result && commits[id.value]) {
          if (!exits[id.value] || op.exceptional != ExceptionalCompletion::kMayThrow ||
              op.normal != LanguageEffect::kGuard || op.before_throw != LanguageEffect::kGuard) return Error::kCatchState;
          const auto& exit = graph.exceptional_exits[*exits[id.value]];
          if (exit.origin != op.origin || exit.before_throw != op.before_throw) return Error::kCatchState;
          std::optional<WorkBlockId> handler;
          bool success = false;
          for (std::size_t e = block.first_edge.value; e < std::size_t(block.first_edge.value) + block.edge_count; ++e) {
            step(); const auto& edge = graph.edges[e];
            if (edge.kind == FlowEdgeKind::kException) {
              if (handler || graph.phases[edge.target.value].phase != ExceptionPhase::kCatch) return Error::kCatchState;
              handler = edge.target;
            } else {
              if (success || edge.target != *commits[id.value]) return Error::kCorrespondence;
              success = true;
            }
          }
          if (!success || handler != (exit.handler ? std::optional{exit.handler->entry} : std::nullopt)) return Error::kCatchState;
          const auto& use = graph.uses[op.first_use.value];
          if (use.owner != decltype(use.owner){id} || use.role != OperandRole::kGuardValue || use.purpose != ReadPurpose::kOperand)
            return Error::kInput;
          const auto* value = std::get_if<ValueId>(&use.operand);
          if (value && value->value < identity.canonical.size()) {
            const auto root = identity.canonical[value->value];
            const auto* result = std::get_if<OpResult>(&graph.values[root.value].origin);
            if (result && result->op.value < graph.ops.size()) {
              const auto read = result->op; const auto& load = graph.ops[read.value];
              if (std::holds_alternative<ValueLexicalLoad>(load.operation) && load.result == root &&
                  load.exceptional == ExceptionalCompletion::kImpossible && prior == State{StateKind::kEvent, read} &&
                  dom.Dominates({load.block.value}, {op.block.value})) {
                if (candidates[read.value]) invalid[read.value] = 1;
                else candidates[read.value] = GuardedRead{read, id, root, *commits[id.value], exit.handler};
              }
            }
          }
        }
        if (Event(op)) prior = {StateKind::kEvent, id};
      }
    }
    for (const auto& use : graph.uses) {
      step(); if (use.life != ValueLife::kLive) continue;
      const auto* value = std::get_if<ValueId>(&use.operand);
      if (!value) continue;
      const auto root = identity.canonical[value->value];
      const auto* result = std::get_if<OpResult>(&graph.values[root.value].origin);
      if (!result || !candidates[result->op.value]) continue;
      const auto& pair = *candidates[result->op.value];
      const auto* owner = std::get_if<OpId>(&use.owner);
      if (owner && *owner == pair.guard) continue;
      if (owner) {
        const auto& op = graph.ops[owner->value];
        // Identity transport is not an observation; its eventual uses are
        // checked here too, including every exceptional phi contribution.
        if (!Event(op) && op.result && identity.canonical[op.result->value] == root) continue;
        if (dom.Dominates({pair.success.value}, {op.block.value})) continue;
      }
      if (const auto* phi = std::get_if<ValueId>(&use.owner);
          phi && identity.canonical[phi->value] == root) continue;
      invalid[pair.read.value] = 1;
    }
    GuardedReads proved{graph.revision, graph.cfg_revision};
    std::size_t count = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) { step(); count += candidates[i].has_value() && !invalid[i]; }
    if (count > memory.Available() / sizeof(GuardedRead)) return Error::kResourceLimit;
    memory.Retain(count * sizeof(GuardedRead)); proved.reads.reserve(count);
    memory.Retain((proved.reads.capacity() - count) * sizeof(GuardedRead));
    for (std::size_t i = 0; i < candidates.size(); ++i) { step(); if (candidates[i] && !invalid[i]) proved.reads.push_back(*candidates[i]); }
    proved.allocated_bytes = memory.Peak(); proved.work = budget.work - remaining.work;
    return proved;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, GuardedReadError> VerifyGuardedReads(
    const ValueProgram& graph, const GuardedReads& proposed, SemanticBudget budget) {
  if (proposed.revision != graph.revision || proposed.cfg_revision != graph.cfg_revision) return Error::kCorrespondence;
  auto built = ProveGuardedReads(graph, budget);
  if (const auto* error = std::get_if<Error>(&built)) return *error;
  const auto& actual = std::get<GuardedReads>(built);
  if (actual.reads != proposed.reads) return Error::kCorrespondence;
  return actual.work;
}
}  // namespace abcd::analysis
