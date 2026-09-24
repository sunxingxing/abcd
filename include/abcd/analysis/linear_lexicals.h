#ifndef ABCD_ANALYSIS_LINEAR_LEXICALS_H_
#define ABCD_ANALYSIS_LINEAR_LEXICALS_H_

#include "abcd/analysis/creation_binding.h"
#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/phi_placement.h"

namespace abcd::analysis {
struct LocalCellId {
  std::uint32_t value = 0;
};
struct LocalCell {
  EnvironmentId environment;
  LexicalSlot slot;
};
struct KeepInstruction {};
struct OmitInstruction {};
struct ClosureCreation {
  // Caller-proven: the child only accesses these captured language cells and
  // never exposes the environment/home object; all child stores are Language.
  // Same unchanged body/context;
  // sorted unique offsets, ordinary DEFINEFUNC only. Not a cached object value.
  abc::CodeOffset offset;
  std::span<const LocalCell> captures = {};
};
// Parametric child analysis only. Its caller must discharge every captured
// binding's Language premise before publishing any executable source.
enum class CapturedValues { kReject, kLanguage };
struct LocalFunction {};
struct LocalLoad {
  LocalCellId cell;
};
struct LocalStore {
  LocalCellId cell;
  ScalarSlot value;
};
struct CapturedLoad {
  CapturedSlot cell;
};
struct CapturedStore {
  CapturedSlot cell;
  ScalarSlot value;
};
struct ExternalImport {
  ModuleSlot slot;
};
struct LocalModuleLoad {
  ModuleSlot slot;
};
using LinearStatement =
    std::variant<KeepInstruction, OmitInstruction, LocalLoad, LocalStore,
                 LocalFunction, CapturedLoad, CapturedStore, NamedInvocation,
                 ExternalImport, LocalModuleLoad>;
enum class LinearLexicalError {
  kExceptionRegions,
  kControlFlow,
  kTerminator,
  kEnvironment,
  kCaptured,
  kUninitialized,
  kInternalValue,
  kMayThrowGuard,
  kInstruction,
  kWorkLimit,
  kResourceLimit
};
struct LinearLexicalFailure {
  LinearLexicalError kind;
  abc::CodeOffset offset{};
};
struct LinearLexicalWork {
  std::uint64_t instructions = 0;
  std::uint64_t units = 0;
  std::size_t peak_bytes = 0;
};
// Same validated straight-line body/frame and unchanged environment flow.
// Entry registers are caller-PROVEN language values; acc starts Hole. Only
// initialized local cells and language reads can reach emitted statements.
// Omissions exclude GC/OOM timing/debugger observability, not language effects.
// Named allocations require a same-body validated scope-literal certificate;
// branches, handlers and possibly failing guards are rejected. Capture requires
// an explicit Language premise; creations require
// complete child binding proofs and initialized parent cells. This is
// neither general CFG promotion nor a function service.
class LinearLexicals {
 public:
  LinearLexicals(LinearLexicals&&) noexcept = default;
  LinearLexicals& operator=(LinearLexicals&&) noexcept = default;
  static std::variant<LinearLexicals, LinearLexicalFailure> Build(
      const abc::MethodBody& body, const EnvironmentFlow& environments,
      std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget = {},
      std::span<const ClosureCreation> creations = {},
      CapturedValues captured_values = CapturedValues::kReject,
      std::span<const std::uint8_t> named_scopes = {},
      std::span<const std::uint8_t> guarded_module_loads = {},
      std::optional<StagedNamedInvocation> staged_call = {});
  std::span<const LinearStatement> Statements() const {
    return {statements_.get(), statements_ ? statement_count_ : 0};
  }
  std::span<const LocalCell> Cells() const {
    return {cells_.get(), cells_ ? cell_count_ : 0};
  }
  std::size_t AllocatedBytes() const {
    return Statements().size_bytes() + Cells().size_bytes();
  }
  LinearLexicalWork work;

 private:
  LinearLexicals() = default;
  std::unique_ptr<LinearStatement[]> statements_;
  std::unique_ptr<LocalCell[]> cells_;
  std::size_t statement_count_ = 0;
  std::size_t cell_count_ = 0;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_LINEAR_LEXICALS_H_
