#include "phi_copy_checks.h"
#include "for_in_plan.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::variant<std::uint64_t, PhiCopyError> VerifyPhiCopiesForFlow(
    const analysis::ValueProgram& graph, const PhiCopies& plan, analysis::SemanticBudget allowance,
    PhiFlow flow, std::span<const ForOfUseDisposition> for_of) {
  try {
    RequirePhiCopy(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, PhiCopyError::kRevision);
    CopyBudget budget(allowance);
    RequirePhiCopy(plan.flow == flow, PhiCopyError::kFlow);
    RequirePhiCopy(plan.domain == PhiDomain::kLanguage ||
        (flow == PhiFlow::kPhased && plan.domain == PhiDomain::kHoleTransport), PhiCopyError::kDomain);
    CheckPhiFlow(graph, budget, flow);
    RequirePhiCopy(plan.inputs.size() == plan.copies.size(), PhiCopyError::kInput);
    RequirePhiCopy(plan.projection.size() == plan.copies.size(),PhiCopyError::kInput);
    const auto identities = PhiIdentities(graph,budget);
    std::optional<ForInPlan> protocol;
    if (!plan.for_in_inputs.empty()) {
      auto built = BuildForInPlan(graph,budget.Remaining());
      if (const auto* error = std::get_if<ForInFailure>(&built))
        throw error->kind == ForInError::kResourceLimit ? PhiCopyError::kResourceLimit :
            error->kind == ForInError::kWorkLimit ? PhiCopyError::kWorkLimit : PhiCopyError::kInput;
      protocol = std::get<ForInPlan>(std::move(built));
      budget.Admit(protocol->allocated_bytes,1); budget.Step(protocol->work);
      std::size_t at = 0;
      for (std::size_t u = 0; u < graph.uses.size(); ++u) {
        budget.Step(); if (!PhiInput(graph.uses[u]) || !protocol->uses[u]) continue;
        RequirePhiCopy(at < plan.for_in_inputs.size() && plan.for_in_inputs[at++].value == u, PhiCopyError::kInput);
      }
      RequirePhiCopy(at == plan.for_in_inputs.size(),PhiCopyError::kInput);
    }
    RequirePhiCopy(for_of.empty() || for_of.size()==graph.uses.size(),PhiCopyError::kInput);
    std::size_t at_of=0;
    if(!for_of.empty())for(std::uint32_t u=0;u<graph.uses.size();++u){budget.Step();if(!PhiInput(graph.uses[u]) || !for_of[u].protocol)continue;RequirePhiCopy(at_of<plan.for_of_inputs.size() && plan.for_of_inputs[at_of++].value==u,PhiCopyError::kInput);}
    RequirePhiCopy(at_of==plan.for_of_inputs.size(),PhiCopyError::kInput);
    std::vector<ParallelCopy> effective; budget.Reserve(effective,plan.copies.size());
    std::vector<std::uint8_t> seen;
    budget.Array(seen, graph.uses.size());
    std::size_t first = 0;
    std::optional<analysis::IncomingKey> previous;
    for (const auto& bundle : plan.bundles) {
      budget.Step();
      RequirePhiCopy(bundle.first == first && bundle.count != 0 &&
          bundle.count <= plan.copies.size() - first &&
          (!previous || IncomingLess(*previous, bundle.incoming)), PhiCopyError::kBundle);
      effective.clear(); std::optional<analysis::UseId> expression_input;
      for (std::size_t i = first; i < first + bundle.count; ++i) {
        budget.Step();
        const auto input = plan.inputs[i];
        RequirePhiCopy(input.value < graph.uses.size() && !seen[input.value], PhiCopyError::kInput);
        seen[input.value] = 1;
        const auto& use = graph.uses[input.value];
        RequirePhiCopy(PhiInput(use) && use.incoming && SameIncoming(*use.incoming, bundle.incoming), PhiCopyError::kInput);
        const bool of=!for_of.empty() && for_of[input.value].protocol.has_value();
        const bool consumed = of || (protocol && protocol->uses[input.value].has_value());
        if (!consumed) CheckPhiDomain(graph, use, plan.domain);
        RequirePhiCopy(std::get<analysis::ValueId>(use.owner) == plan.copies[i].destination &&
            SameCopyValue(use.operand, plan.copies[i].source), PhiCopyError::kInput);
        const bool identity = SourcePhiAlias(graph,identities,plan.copies[i].destination).has_value();
        RequirePhiCopy(plan.projection[i] == (of ? PhiCopyUse::kForOfProtocol : consumed ? PhiCopyUse::kForInProtocol :
            identity ? PhiCopyUse::kIdentity : PhiCopyUse::kRetained),PhiCopyError::kInput);
        if (!consumed && !identity) { effective.push_back(plan.copies[i]); expression_input = input; }
      }
      if (effective.size() != 1) expression_input.reset();
      RequirePhiCopy(bundle.expression_input == expression_input,PhiCopyError::kInput);
      const auto checked = VerifyEdgeCopies(effective, bundle.schedule, budget.Remaining());
      if (const auto* error = std::get_if<CopyError>(&checked)) throw PhiScheduleError(*error);
      budget.Step(std::get<std::uint64_t>(checked));
      first += bundle.count;
      previous = bundle.incoming;
    }
    RequirePhiCopy(first == plan.copies.size(), PhiCopyError::kBundle);
    for (std::size_t i = 0; i < graph.uses.size(); ++i) {
      budget.Step(); RequirePhiCopy(bool(seen[i]) == PhiInput(graph.uses[i]), PhiCopyError::kInput);
    }
    return budget.Work();
  } catch (PhiCopyError error) { return error;
  } catch (CopyError error) { return PhiScheduleError(error);
  } catch (const std::bad_alloc&) { return PhiCopyError::kResourceLimit;
  } catch (const std::length_error&) { return PhiCopyError::kResourceLimit; }
}
std::variant<std::uint64_t, PhiCopyError> VerifyPhiCopies(
    const analysis::ValueProgram& graph, const PhiCopies& plan, analysis::SemanticBudget allowance) {
  return VerifyPhiCopiesForFlow(graph, plan, allowance, PhiFlow::kNormal);
}
std::variant<std::uint64_t, PhiCopyError> VerifyPhasedPhiCopies(
    const analysis::ValueProgram& graph, const PhiCopies& plan, analysis::SemanticBudget allowance, std::span<const ForOfUseDisposition> for_of) {
  return VerifyPhiCopiesForFlow(graph, plan, allowance, PhiFlow::kPhased,for_of);
}
}  // namespace abcd::source
