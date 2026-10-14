#include "phi_copy_checks.h"
#include "literal_plan.h"
#include "for_in_plan.h"
#include "tree_checks.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
std::variant<PhiCopies, PhiCopyError> BuildCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget allowance, PhiFlow flow,
    PhiDomain domain = PhiDomain::kLanguage, const ForInPlan* for_in = nullptr) {
  try {
    CopyBudget budget(allowance);
    CheckPhiFlow(graph, budget, flow);
    RequirePhiCopy(graph.uses.size() <= std::numeric_limits<std::uint32_t>::max(), PhiCopyError::kResourceLimit);
    std::size_t count = 0;
    for (const auto& use : graph.uses) { budget.Step(); count += PhiInput(use); }
    PhiCopies result{graph.revision, graph.cfg_revision, {}, {}, {}};
    result.flow = flow;
    result.domain = domain;
    budget.Admit(1, sizeof(result));
    budget.Reserve(result.inputs, count);
    budget.Reserve(result.copies, count);
    if (for_in) {
      // The common evaluation entry proved this certificate before phi work.
      // VerifyPhiCopiesForFlow independently reconstructs it below.
      RequirePhiCopy(for_in->uses.size() == graph.uses.size(), PhiCopyError::kInput);
      budget.Reserve(result.for_in_inputs, count);
    }
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      budget.Step();
      const auto& use = graph.uses[i];
      if (!PhiInput(use)) continue;
      if (for_in && for_in->uses[i]) result.for_in_inputs.push_back({static_cast<std::uint32_t>(i)});
      else CheckPhiDomain(graph, use, domain);
      result.inputs.push_back({static_cast<std::uint32_t>(i)});
    }
    std::sort(result.inputs.begin(), result.inputs.end(), [&](auto a, auto b) {
      budget.Step();
      const auto& left = *graph.uses[a.value].incoming;
      const auto& right = *graph.uses[b.value].incoming;
      return SameIncoming(left, right) ? a < b : IncomingLess(left, right);
    });
    std::size_t bundles = 0;
    for (std::size_t i = 0; i < result.inputs.size(); ++i) {
      budget.Step();
      const auto& use = graph.uses[result.inputs[i].value];
      if (i == 0 || !SameIncoming(*use.incoming, *graph.uses[result.inputs[i - 1].value].incoming)) ++bundles;
      result.copies.push_back({std::get<analysis::ValueId>(use.owner), use.operand});
    }
    budget.Reserve(result.bundles, bundles);
    std::size_t schedule_bytes = 0;
    const auto identities = PhiIdentities(graph,budget);
    budget.Array(result.projection,count);
    std::vector<ParallelCopy> effective; budget.Reserve(effective,count);
    for (std::size_t first = 0; first < count;) {
      budget.Step();
      const auto key = *graph.uses[result.inputs[first].value].incoming;
      auto end = first + 1;
      while (end < count && SameIncoming(key, *graph.uses[result.inputs[end].value].incoming)) { budget.Step(); ++end; }
      effective.clear(); std::optional<analysis::UseId> expression_input;
      for (auto i=first;i<end;++i) {
        budget.Step();
        if (for_in && for_in->uses[result.inputs[i].value]) {
          result.projection[i] = PhiCopyUse::kForInProtocol; continue;
        }
        if (SourcePhiAlias(graph,identities,result.copies[i].destination)) {
          result.projection[i] = PhiCopyUse::kIdentity; continue;
        }
        effective.push_back(result.copies[i]); expression_input = result.inputs[i];
      }
      if (effective.size() != 1) expression_input.reset();
      auto built = BuildEdgeCopies(effective, budget.Remaining());
      auto* schedule = std::get_if<EdgeCopies>(&built);
      if (!schedule) throw PhiScheduleError(std::get<CopyError>(built));
      budget.Admit(schedule->allocated_bytes, 1); budget.Step(schedule->work);
      schedule_bytes += schedule->allocated_bytes; // Already bounded by the shared byte grant.
      result.bundles.push_back({key, static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(end - first), std::move(*schedule),expression_input});
      first = end;
    }
    const auto checked = VerifyPhiCopiesForFlow(graph, result, budget.Remaining(), flow);
    if (const auto* error = std::get_if<PhiCopyError>(&checked)) return *error;
    budget.Step(std::get<std::uint64_t>(checked));
    result.allocated_bytes = sizeof(result) + schedule_bytes +
        (result.inputs.capacity() + result.for_in_inputs.capacity()) * sizeof(analysis::UseId) + result.copies.capacity() * sizeof(ParallelCopy) +
        result.projection.capacity() * sizeof(PhiCopyUse) +
        result.bundles.capacity() * sizeof(PhiBundle);
    result.work = budget.Work();
    return result;
  } catch (PhiCopyError error) { return error;
  } catch (CopyError error) { return PhiScheduleError(error);
  } catch (const std::bad_alloc&) { return PhiCopyError::kResourceLimit;
  } catch (const std::length_error&) { return PhiCopyError::kResourceLimit; }
}
}  // namespace
std::variant<PhiCopies, PhiCopyError> BuildPhiCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget allowance) {
  return BuildCopies(graph, allowance, PhiFlow::kNormal);
}
std::variant<PhiCopies, PhiCopyError> BuildPhasedPhiCopies(
    const analysis::ValueProgram& graph, analysis::SemanticBudget allowance, PhiDomain domain, const ForInPlan* for_in) {
  return BuildCopies(graph, allowance, PhiFlow::kPhased, domain, for_in);
}
std::vector<std::optional<analysis::OpId>> IndexPhiLiteralEnds(
    const analysis::ValueProgram& graph, const LiteralPlan& literals, TreeCheckBudget& budget) {
  std::vector<std::optional<analysis::OpId>> ends;
  if (!graph.phases.empty()) return ends;
  budget.Array(ends, graph.ops.size());
  if (literals.arrays) for (const auto& array : literals.arrays->arrays) {
    budget.Step(); ends[array.allocation.value] = literals.arrays->steps[array.first + array.count - 1].operation;
  }
  for (const auto& object : literals.objects) {
    budget.Step(); if (object.form != LiteralForm::kExpression) continue;
    const auto& allocation = graph.ops[object.allocation.value];
    auto last = object.allocation; bool same_block = true;
    for (std::uint32_t i = 0; i < object.count; ++i) {
      budget.Step(); const auto id = literals.definitions[object.first + i];
      same_block &= graph.ops[id.value].block == allocation.block;
      last = std::max(last, id);
    }
    if (same_block) ends[object.allocation.value] = last;
  }
  return ends;
}
}  // namespace abcd::source
