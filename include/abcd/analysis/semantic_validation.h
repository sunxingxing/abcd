#ifndef ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_
#define ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_

#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/linear_lexicals.h"
#include "abcd/analysis/register_value_flow.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
struct CapturedReadFact {
  InstructionId instruction;
  CapturedSlot cell;
};
enum class ModuleReadProof : std::uint8_t {
  kGuarded, kInitialized, kCallerGuarded, kCatchGuarded
};
constexpr bool IsGuardedModuleRead(ModuleReadProof proof) {
  switch (proof) {
    case ModuleReadProof::kGuarded:
    case ModuleReadProof::kCallerGuarded:
    case ModuleReadProof::kCatchGuarded: return true;
    case ModuleReadProof::kInitialized: return false;
  }
  return false;
}
struct ModuleReadFact {
  InstructionId instruction;
  ModuleReadProof proof;
};
enum class LocalAccessKind : std::uint8_t { kLoad, kStore };
struct LocalAccessFact {
  InstructionId instruction;
  LocalCellId cell;
  LocalAccessKind kind;
};
struct LocalStoreSite {
  LocalCellId cell;
  InstructionId instruction;
};
struct LocalInitialization {
  InstructionId store;
  std::uint32_t begin;
  std::uint32_t end;
};
struct LocalAccessPlan {
  std::vector<LocalCell> cells;
  std::vector<LocalAccessFact> accesses;
  std::vector<LocalStoreSite> stores;
  std::vector<std::optional<InstructionId>> allocations;
  // Forward-only CFG certificate: allocation and initial store share a block;
  // its dominator subtree covers these block-entry numbers. Later stores must
  // independently pass Language verification. No closure mutation is inferred.
  std::vector<std::optional<LocalInitialization>> initializations;
  std::vector<std::uint32_t> block_entries;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return cells.capacity() * sizeof(LocalCell) +
           accesses.capacity() * sizeof(LocalAccessFact) +
           stores.capacity() * sizeof(LocalStoreSite) +
           allocations.capacity() * sizeof(std::optional<InstructionId>) +
           initializations.capacity() * sizeof(std::optional<LocalInitialization>) +
           block_entries.capacity() * sizeof(std::uint32_t);
  }
};
struct SemanticProof {
  EnvironmentFlow environments;
  RegisterValueFlow values;
  std::size_t allocated_bytes = 0;
  // Load + adjacent static-name guard may become a throwing source binding
  // read. This is a lowering proof, not a claim that raw loads cannot be Hole.
  // Initialized facts instead depend on an explicit callable-owner premise.
  std::vector<ModuleReadFact> module_reads;
  // Parametric premises, not resolved source bindings. The owner must rebase
  // every emitted capture and discharge its Language premise before publication.
  std::vector<CapturedReadFact> captured_reads;
  LocalAccessPlan locals;
};
enum class SemanticProofError : std::uint8_t {
  kAccumulator,  // Retained serialized value; generic proof now uses SSA.
  kEnvironment,
  kRegisterValue,
  kInternalValue,
  kSsaMismatch,
  kResourceLimit,
  kModulePremise,
};
struct SemanticProofFailure {
  SemanticProofError kind;
  abc::CodeOffset pc;
};
using SemanticProofResult = std::variant<SemanticProof,
                                         SemanticProofFailure>;

// Source admission for the unchanged decoded body/semantic method pair.
// Only ABI register entries are Language; SSA checks every actual read, with
// unknown local-register/accumulator entries. Local loads require same-block
// allocation and a preceding store, before any capture creation. Every store
// must pass the same SSA Language check; no cell defaults to JS undefined.
// An adjacent module load/static-name guard in one block may
// lower to a throwing live-binding read; module_reads records that proof.
// initialized_modules are sorted, unique same-body LOCAL load sites whose
// owner proves initialization before invocation, with Language-only later
// writes. They are not inferred from an export name or an adjacent guard.
// kLanguage enables parametric captured reads only; every resulting capture
// must be bound and its premise discharged by the parent before publication.
// Local cells use the separate initialization proof, not this option. The printer must separately
// prove same-record module binding identity and slot validity.
[[nodiscard]] SemanticProofResult VerifySemanticSource(
    const abc::MethodBody& body, const SemanticMethod& method,
    std::uint64_t frame_size, std::uint32_t first_abi_register,
    SemanticBudget budget = {},
    CapturedValues captures = CapturedValues::kReject,
    std::span<const LanguageLoadFact> initialized_modules = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_
