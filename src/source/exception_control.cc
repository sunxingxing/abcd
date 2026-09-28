#include "exception_topology.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ExceptionControlError;
void Require(bool valid, Error error) { if (!valid) throw error; }
Error Translate(ControlError error) {
  switch (error) {
    case ControlError::kRevision: return Error::kRevision;
    case ControlError::kCycle: return Error::kCycle;
    case ControlError::kCrossEntry: return Error::kCrossEntry;
    case ControlError::kWorkLimit: return Error::kWorkLimit;
    case ControlError::kResourceLimit: return Error::kResourceLimit;
    case ControlError::kInvalidGraph: case ControlError::kUncoveredBlock:
    case ControlError::kIrreducible: case ControlError::kLoopExits: return Error::kStructure;
  }
  return Error::kStructure;
}
void CheckInput(const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, const ExceptionPlan& regions, ControlQuota& quota) {
  const auto checked = VerifyExceptionPlan(body, graph, input, regions, quota.Remaining());
  if (const auto* error = std::get_if<ExceptionPlanError>(&checked)) {
    switch (*error) {
      case ExceptionPlanError::kResourceLimit: throw Error::kResourceLimit;
      case ExceptionPlanError::kWorkLimit: throw Error::kWorkLimit;
      case ExceptionPlanError::kRevision: throw Error::kRevision;
      case ExceptionPlanError::kInput: case ExceptionPlanError::kLayout:
      case ExceptionPlanError::kBinding: case ExceptionPlanError::kTransfer: throw Error::kInput;
    }
  }
  quota.Work(std::get<std::uint64_t>(checked));
}
template <typename T> void Compare(const std::vector<T>& expected, const std::vector<T>& actual,
                                   ControlQuota& quota) {
  Require(expected.size() == actual.size(), Error::kStructure);
  for (std::size_t i = 0; i < expected.size(); ++i) {
    quota.Work(); Require(expected[i] == actual[i], Error::kStructure);
  }
}
void CheckProjection(const ValueProgram& graph, const ExceptionTopology& topology,
                     const ExceptionControl& actual, ControlQuota& quota) {
  const auto& mapping = topology.mapping;
  Compare(mapping.units, actual.units, quota);
  Compare(mapping.arcs, actual.arcs, quota);
  Compare(mapping.regions, actual.regions, quota);
  Compare(mapping.inactive_commits, actual.inactive_commits, quota);
  VerifyControlSkeleton(ControlGraphView{ControlProjection{graph.revision, graph.cfg_revision, topology.blocks, topology.edges}},
      topology.entries, actual.skeleton, quota);
}
}
std::variant<ExceptionControl, ExceptionControlError> BuildExceptionControl(
    const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, const ExceptionPlan& regions, SemanticBudget budget) {
  try {
    ControlQuota quota(budget);
    CheckInput(body, graph, input, regions, quota);
    auto topology = ProjectExceptionControl(graph, regions, quota);
    auto skeleton = BuildControlSkeleton(
        ControlGraphView{ControlProjection{graph.revision, graph.cfg_revision, topology.blocks, topology.edges}}, topology.entries, quota);
    VerifyControlSkeleton(ControlGraphView{ControlProjection{graph.revision, graph.cfg_revision, topology.blocks, topology.edges}},
        topology.entries, skeleton, quota);
    auto result = std::move(topology.mapping);
    result.skeleton = std::move(skeleton);
    result.allocated_bytes = result.skeleton.allocated_bytes + result.units.capacity() * sizeof(ExceptionControlUnit) +
        result.arcs.capacity() * sizeof(ControlArc) + result.regions.capacity() * sizeof(TryControlRegion) +
        result.inactive_commits.capacity() * sizeof(WorkBlockId);
    result.work = budget.work - quota.RemainingWork();
    return result;
  } catch (Error error) { return error;
  } catch (ControlError error) { return Translate(error);
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, ExceptionControlError> VerifyExceptionControl(
    const abc::MethodBody& body, const ValueProgram& graph,
    const PhasedValueInput& input, const ExceptionPlan& regions,
    const ExceptionControl& control, SemanticBudget budget) {
  try {
    ControlQuota quota(budget);
    CheckInput(body, graph, input, regions, quota);
    const auto topology = ProjectExceptionControl(graph, regions, quota);
    CheckProjection(graph, topology, control, quota);
    return budget.work - quota.RemainingWork();
  } catch (Error error) { return error;
  } catch (ControlError error) { return Translate(error);
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
