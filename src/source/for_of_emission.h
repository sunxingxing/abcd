#ifndef ABCD_SOURCE_FOR_OF_EMISSION_H_
#define ABCD_SOURCE_FOR_OF_EMISSION_H_
#include "for_of_evaluation_plan.h"
namespace abcd::source {
struct ForOfEmissionLoop {
  IteratorProtocolId protocol;
  LoopId loop;
  ControlId header, decision, body;
  analysis::EdgeId success, exhausted;
  analysis::UseId iterable;
  analysis::ValueId binding;
  bool exhaustion_only=true;
  std::optional<IteratorProtocolId> enclosing;
  bool operator==(const ForOfEmissionLoop&) const = default;
};
// A close remains effectful. These are entry-qualified native completion
// ports, never transparent handler forwarding or permission to erase effects.
struct ForOfUndefined { bool operator==(const ForOfUndefined&) const = default; };
using ForOfExitPayload=std::variant<analysis::ValueId,ForOfUndefined>;
struct ForOfEmissionClose {
  IteratorProtocolId protocol;
  analysis::CompletionEdge entry;
  analysis::PendingCompletion pending;
  std::uint32_t first_port, port_count;
  analysis::EdgeId edge;
  std::optional<ForOfExitPayload> payload;
  bool operator==(const ForOfEmissionClose&) const = default;
};
struct ForOfEmissionView {
  std::shared_ptr<const CompletionPorts> completion_ports;
  std::shared_ptr<const IteratorContinuationProof> continuations;
  ProtocolForwarding forwarding;
  std::vector<ForOfEmissionLoop> loops;
  std::vector<std::optional<std::uint32_t>> wrappers;
  std::vector<std::optional<ForOfEmissionClose>> closes;
  std::vector<ForOfClosePort> ports;
  std::vector<std::uint8_t> close_blocks;
  std::size_t allocated_bytes=0, peak_bytes=0;
  std::uint64_t work=0;
};
std::variant<ForOfEmissionView,ForOfIterationFailure> BuildForOfEmissionView(
    const analysis::ValueProgram&,const ForOfEvaluationInput&,const ForOfEvaluationPlan&,
    analysis::SemanticBudget = {});
std::variant<std::uint64_t,ForOfIterationFailure> VerifyForOfEmissionView(
    const analysis::ValueProgram&,const ForOfEvaluationInput&,const ForOfEvaluationPlan&,
    const ForOfEmissionView&,analysis::SemanticBudget = {});
}
#endif
