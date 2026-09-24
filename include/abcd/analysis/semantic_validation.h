#ifndef ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_
#define ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_

#include "abcd/analysis/accumulator_flow.h"
#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/register_value_flow.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
struct SemanticProof {
  AccumulatorFlow accumulator;
  EnvironmentFlow environments;
  RegisterValueFlow values;
  std::size_t allocated_bytes = 0;
  // Load + adjacent static-name guard may become a throwing source binding
  // read. This is a lowering proof, not a claim that raw loads cannot be Hole.
  std::vector<LanguageLoadFact> guarded_modules;
};
enum class SemanticProofError : std::uint8_t {
  kAccumulator,
  kEnvironment,
  kRegisterValue,
  kInternalValue,
  kSsaMismatch,
  kResourceLimit,
};
struct SemanticProofFailure {
  SemanticProofError kind;
  abc::CodeOffset pc;
};
using SemanticProofResult = std::variant<SemanticProof,
                                         SemanticProofFailure>;

// Source admission for the unchanged decoded body/semantic method pair.
// Entry registers are ABI language values; entry accumulator is not assumed
// initialized. An adjacent module load/static-name guard in one block may
// lower to a throwing live-binding read; guarded_modules records that proof.
// Unpaired loads and lexical Hole refinement remain unsupported. The printer
// must separately prove same-record binding identity and slot validity.
[[nodiscard]] SemanticProofResult VerifySemanticSource(
    const abc::MethodBody& body, const SemanticMethod& method,
    std::uint64_t frame_size, std::uint32_t first_abi_register,
    SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_
