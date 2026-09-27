#ifndef ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_
#define ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_

#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/capture_effect.h"
#include "abcd/analysis/linear_lexicals.h"
#include "abcd/analysis/register_value_flow.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
struct CapturedReadFact {
  InstructionId instruction;
  CapturedSlot cell;
};
// Conditional on complete child-template validation and capture rebasing.
// kLanguage preserves the value domain, NOT identity, non-undefinedness or
// immutability. Every transitive child write must discharge this premise.
enum class ClosureWrites : std::uint8_t { kUnknown, kLanguage };
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
struct CaptureGuard {
  LocalCellId cell;
  InstructionId instruction;
  std::uint32_t begin;
  std::uint32_t end;
};
struct DefinedCaptures {
  // Disjoint dominator intervals, sorted by (cell, begin), and block entries.
  // Valid only with no later direct stores or transitive closure writes.
  std::vector<CaptureGuard> guards;
  std::vector<std::uint32_t> block_entries;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return guards.capacity() * sizeof(CaptureGuard) +
           block_entries.capacity() * sizeof(std::uint32_t);
  }
};
struct LocalAccessPlan {
  std::vector<LocalCell> cells;
  std::vector<LocalAccessFact> accesses;
  std::vector<LocalStoreSite> stores;
  std::vector<std::optional<InstructionId>> allocations;
  // Forward-only CFG certificate: allocation dominates the initial store;
  // the store's dominator subtree covers these block-entry numbers. Later stores must
  // independently pass Language verification. No closure mutation is inferred.
  std::vector<std::optional<LocalInitialization>> initializations;
  std::vector<std::uint32_t> block_entries;
  // Sorted creation instructions lacking same-body CapturePreservation.
  // Includes callable/class/buffer captures; not merely the previous function.
  // Empty means the verifier found no unpreserved sites, not initialization.
  std::vector<InstructionId> capture_barriers;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const {
    return cells.capacity() * sizeof(LocalCell) +
           accesses.capacity() * sizeof(LocalAccessFact) +
           stores.capacity() * sizeof(LocalStoreSite) +
           allocations.capacity() * sizeof(std::optional<InstructionId>) +
           initializations.capacity() * sizeof(std::optional<LocalInitialization>) +
           block_entries.capacity() * sizeof(std::uint32_t) +
           capture_barriers.capacity() * sizeof(InstructionId);
  }
};
// Caller-certified invocation, not merely a layout. Under the existing legal
// JS entry contract its actual count and extra tail belong to this callable;
// the tail contains language values and cannot be addressed by declared vregs.
// Source owners must retain/recheck their creation identity. This fact does not
// authorize native JS arguments, arrow capture, or any source signature rewrite.
struct ArgumentFrameFact {
  abc::FrameLayout frame;
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
  ClosureWrites closure_writes = ClosureWrites::kUnknown;
  DefinedCaptures defined_captures;
  FrameReceiver receiver = FrameReceiver::kUnknown;
  std::optional<ArgumentFrameFact> arguments;
};
enum class SemanticProofError : std::uint8_t {
  kAccumulator,  // Retained serialized value; generic proof now uses SSA.
  kEnvironment,
  kRegisterValue,
  kInternalValue,
  kSsaMismatch,
  kResourceLimit,
  kModulePremise,
  kArgumentOwner,
};
struct SemanticProofFailure {
  SemanticProofError kind;
  abc::CodeOffset pc;
};
using SemanticProofResult = std::variant<SemanticProof,
                                         SemanticProofFailure>;

// Source admission for the unchanged decoded body/semantic method pair.
// Only ABI register entries are Language; SSA checks every actual read, with
// unknown local-register/accumulator entries. Local loads require the local
// initialization certificate; by default capture creation blocks later loads.
// Every store must pass the same SSA Language check; no cell defaults to JS
// undefined.
// An adjacent module load/static-name guard in one block may
// lower to a throwing live-binding read; module_reads records that proof.
// initialized_modules are sorted, unique same-body LOCAL load sites whose
// owner proves initialization before invocation, with Language-only later
// writes. They are not inferred from an export name or an adjacent guard.
// kLanguage enables parametric captured reads only; every resulting capture
// must be bound and its premise discharged by the parent before publication.
// Local cells use the separate initialization proof, not this option. The printer must separately
// prove same-record module binding identity and slot validity.
// closure_writes=kLanguage is a separate owner premise: every created callable
// must pass complete child-template emission with Language-only captured
// stores, including descendants, before the parent is publishable. This admits
// local reads after creation; it does not prove any captured value unchanged.
// preserved contains sorted, unique same-body creation PCs certified not to
// write incoming lexical cells. It removes only those capture barriers, never
// initialization or store-domain checks. The owning file/profile is unchanged.
// calls constrains normal call results under an execution contract; every
// read (including callees, arguments and explicit returns) is still checked.
// receiver is a caller-proven callable-owner premise, retained in the proof.
// It constrains frame.thisObj only, never the writable ABI register copy;
// derived initialization and arrow capture ownership require distinct proofs.
// arguments is a caller-certified current invocation, not one inferred from
// GETUNMAPPEDARGS or a parameter count. Correspondence and every current-slot
// value are checked independently; no absent-slot/Hole proof is waived.
[[nodiscard]] SemanticProofResult VerifySemanticSource(
    const abc::MethodBody& body, const SemanticMethod& method,
    std::uint64_t frame_size, std::uint32_t first_abi_register,
    SemanticBudget budget = {},
    CapturedValues captures = CapturedValues::kReject,
    std::span<const LanguageLoadFact> initialized_modules = {},
    ClosureWrites closure_writes = ClosureWrites::kUnknown,
    std::span<const CapturePreservation> preserved = {},
    CallResultValues calls = CallResultValues::kUnknown,
    FrameReceiver receiver = FrameReceiver::kUnknown,
    const ArgumentFrameFact* arguments = nullptr);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_VALIDATION_H_
