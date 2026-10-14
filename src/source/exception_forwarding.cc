#include "exception_forwarding.h"
#include "for_in_quota.h"
#include "../analysis/value_phi.h"
#include <new>
#include <stdexcept>

namespace abcd::source {
using namespace analysis;
std::variant<ExceptionForwarding, ForInFailure> AnalyzeExceptionForwarding(
    const ValueProgram& graph, SemanticBudget budget) {
  detail::ForInQuota quota{budget};
  try {
    auto indexed = AnalyzeValueIdentities(graph, quota.Remaining());
    if (const auto* error = std::get_if<ValueSimplifyError>(&indexed))
      quota.Fail(*error == ValueSimplifyError::kWorkLimit ? ForInError::kWorkLimit :
          *error == ValueSimplifyError::kResourceLimit ? ForInError::kResourceLimit : ForInError::kIdentity);
    auto identities = std::get<ValueIdentities>(std::move(indexed));
    quota.memory.Retain(identities.allocated_bytes); quota.Step(identities.work);
    const auto count = graph.blocks.size();
    ExceptionForwarding result;
    quota.Reserve(result.blocks, count); result.blocks.resize(count);
    std::pmr::vector<std::optional<ValueExceptionalExit>> exits(graph.ops.size(), {}, &quota.memory);
    std::pmr::vector<std::uint32_t> pending(count, 0, &quota.memory), offsets(count + 1, 0, &quota.memory);
    std::pmr::vector<WorkBlockId> predecessors(graph.edges.size(), {}, &quota.memory), queue(&quota.memory);
    std::pmr::vector<std::uint8_t> possible(count, 1, &quota.memory), terminal(count, 0, &quota.memory);
    queue.reserve(count);
    for (const auto& exit : graph.exceptional_exits) {
      quota.Step(); quota.Require(exit.op.value < exits.size() && !exits[exit.op.value]);
      exits[exit.op.value] = exit;
    }
    for (const auto& edge : graph.edges) {
      quota.Step(); quota.Require(edge.source.value < count && edge.target.value < count);
      if (edge.kind == FlowEdgeKind::kException) continue;
      ++pending[edge.source.value]; ++offsets[edge.target.value + 1];
    }
    for (std::size_t b = 1; b < offsets.size(); ++b) { quota.Step(); offsets[b] += offsets[b - 1]; }
    auto cursor = std::pmr::vector<std::uint32_t>{offsets, &quota.memory};
    for (const auto& edge : graph.edges) {
      quota.Step(); if (edge.kind != FlowEdgeKind::kException) predecessors[cursor[edge.target.value]++] = edge.source;
    }
    for (std::uint32_t b = 0; b < count; ++b) {
      quota.Step(); const auto& block = graph.blocks[b];
      quota.Require(block.first_op.value <= graph.ops.size() && block.op_count <= graph.ops.size() - block.first_op.value);
      for (std::uint32_t n = 0; n < block.op_count; ++n) {
        quota.Step(); const OpId id{block.first_op.value + n}; const auto& op = graph.ops[id.value];
        if (op.life == ValueLife::kRemoved) continue;
        if (std::holds_alternative<ValueThrow>(op.operation)) {
          quota.Require(op.use_count == 1 && op.first_use.value < graph.uses.size());
          const auto& use = graph.uses[op.first_use.value];
          const auto* value = std::get_if<ValueId>(&use.operand);
          if (value && exits[id.value] && use.role == OperandRole::kThrownValue &&
              op.exceptional == ExceptionalCompletion::kAlwaysThrow &&
              op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone) {
            result.blocks[b] = ForwardedException{identities.canonical[value->value], exits[id.value]->handler};
            terminal[b] = 1;
          } else possible[b] = 0;
        } else if (!detail::Inert(op) || terminal[b]) possible[b] = 0;
      }
      if (!possible[b]) result.blocks[b].reset();
      if (terminal[b] || pending[b] == 0) queue.push_back({b});
    }
    // Reverse Kahn: a cycle never acquires a terminating summary. No repeated
    // handler walks; each ordinary arc participates once, including joins.
    for (std::size_t at = 0; at < queue.size(); ++at) {
      quota.Step(); const auto b = queue[at].value;
      for (auto p = offsets[b]; p < offsets[b + 1]; ++p) {
        quota.Step(); const auto pred = predecessors[p].value;
        if (terminal[pred]) continue;
        quota.Require(pending[pred] != 0);
        if (!result.blocks[b]) possible[pred] = 0;
        else if (result.blocks[pred] && result.blocks[pred] != result.blocks[b]) possible[pred] = 0;
        else result.blocks[pred] = result.blocks[b];
        if (--pending[pred] == 0) {
          if (!possible[pred]) result.blocks[pred].reset();
          queue.push_back({pred});
        }
      }
    }
    for (std::size_t b = 0; b < count; ++b) {
      quota.Step(); if (pending[b] && !terminal[b]) result.blocks[b].reset();
    }
    result.allocated_bytes = quota.memory.Peak(); result.work = quota.initial - quota.remaining;
    return result;
  } catch (ForInFailure failure) { return failure;
  } catch (const std::bad_alloc&) { quota.witness.kind = ForInError::kResourceLimit; return quota.witness;
  } catch (const std::length_error&) { quota.witness.kind = ForInError::kResourceLimit; return quota.witness; }
}
}  // namespace abcd::source
