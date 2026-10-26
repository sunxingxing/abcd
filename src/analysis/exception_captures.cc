#include "abcd/analysis/exception_captures.h"

#include <stdexcept>

#include "abcd/analysis/environment_renewal.h"
#include "idf_scratch.h"

namespace abcd::analysis {
std::variant<ExceptionCaptures, ExceptionCaptureError> PlanExceptionCaptures(
    const ExceptionFlow& flow, const EnvironmentFlow& environments, SemanticBudget budget) {
  using Error = ExceptionCaptureError;
  try {
    if (environments.renewal) {
      const auto& e = *environments.renewal;
      if (e.flow.get() != &flow) return Error::kInvalidEnvironment;
      const auto verified =
          VerifyRenewedEnvironmentFlow(e, environments, budget);
      if (!std::holds_alternative<std::uint64_t>(verified))
        return Error::kInvalidEnvironment;
      const auto work = std::get<std::uint64_t>(verified);
      if (work > budget.work) return Error::kInvalidEnvironment;
      budget.work -= work;
    }
    ExceptionCaptures result;
    const auto charge = [&] {
      if (result.work_units == budget.work) throw Error::kResourceLimit;
      ++result.work_units;
    };
    if (flow.nodes.size() != environments.before.size() || flow.nodes.size() > UINT32_MAX)
      return Error::kInvalidEnvironment;
    detail::IdfScratch memory{budget.bytes};
    std::pmr::vector<std::optional<ExceptionNodeId>> commits(flow.instructions.size(), {}, &memory);
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
      charge();
      const auto& node = flow.nodes[i];
      if (node.instruction.value >= commits.size()) return Error::kInvalidEnvironment;
      if (node.phase != ExceptionPhase::kCommit) continue;
      if (commits[node.instruction.value]) return Error::kInvalidEnvironment;
      commits[node.instruction.value] = ExceptionNodeId{static_cast<std::uint32_t>(i)};
    }
    const auto reserve = [&]<class T>(std::vector<T>& values) {
      if (environments.bindings.size() > memory.Available() / sizeof(T)) throw Error::kResourceLimit;
      values.reserve(environments.bindings.size());
      memory.Retain(values.capacity() * sizeof(T));
    };
    reserve(result.reads);
    reserve(result.loads);
    for (const auto& binding : environments.bindings) {
      charge();
      const auto* depth = std::get_if<CapturedDepth>(&binding.binding.environment);
      if (!depth) continue;
      if (binding.instruction >= flow.nodes.size()) return Error::kInvalidEnvironment;
      const auto& node = flow.nodes[binding.instruction];
      if (node.phase != ExceptionPhase::kBefore || !environments.before[binding.instruction])
        return Error::kInvalidEnvironment;
      if (!std::holds_alternative<LexicalLoad>(flow.instructions[node.instruction.value].operation)) continue;
      const auto commit = commits[node.instruction.value];
      if (!commit || !environments.before[commit->value]) return Error::kInvalidEnvironment;
      result.reads.push_back({node.instruction, {*depth, binding.binding.slot}});
      result.loads.push_back({*commit});
    }
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
