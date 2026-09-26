#ifndef ABCD_SOURCE_DEFAULT_CANDIDATES_H_
#define ABCD_SOURCE_DEFAULT_CANDIDATES_H_

#include "parameter_plan.h"

namespace abcd::source {
struct DefaultCandidate {
  std::uint32_t parameter;
  analysis::ValueId entry;
  analysis::ValueId merged;
  analysis::ValueOperand fallback;
  std::optional<analysis::OpId> initializer;
  analysis::OpId comparison;
  analysis::OpId branch;
  analysis::WorkBlockId header;
  analysis::WorkBlockId arm;
  analysis::WorkBlockId join;
  analysis::EdgeId present;
  analysis::EdgeId missing;
  analysis::EdgeId merge;
};
struct DefaultCandidates {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<DefaultCandidate> values;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class DefaultCandidateError : std::uint8_t {
  kRevision, kInvalidGraph, kResourceLimit, kWorkLimit
};
// Candidate discovery only, NOT permission to erase CFG/phi or move defaults
// into a signature. Later proof must check entry escapes, initializer scope,
// arguments/length and signature origins. Requires verified simplified normal
// IR. Stops at the first non-default prologue; never scans a body looking for
// coincidental undefined tests. No old SourceAst or register identity input.
[[nodiscard]] std::variant<DefaultCandidates, DefaultCandidateError>
FindDefaultCandidates(const analysis::ValueProgram& graph,
    const ParameterPlan& parameters, std::uint32_t length,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
