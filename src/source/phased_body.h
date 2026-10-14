#ifndef ABCD_SOURCE_PHASED_BODY_H_
#define ABCD_SOURCE_PHASED_BODY_H_

#include "exception_tree.h"
#include "function_context.h"
#include "module_slot_set.h"
#include "module_aliases.h"

namespace abcd::source {
struct PhasedModuleBinding;
struct ModuleAliases;
enum class PhasedMethodError : std::uint8_t {
  kInput, kSsa, kEnvironment, kValues, kImport, kRegion, kControl, kEvaluation,
  kResourceLimit, kWorkLimit, kContract, kModules, kCells
};
// Preserve the exact failed input proof and consumed analysis work. No graph
// or input certificate is constructed from this envelope.
struct PhasedFlowFailure {
  PhasedMethodError kind;
  std::variant<analysis::SemanticFailure, analysis::FlowFailure, analysis::DispatchFailure> cause;
  abc::EntityOffset method;
  std::optional<abc::CodeOffset> pc, working_pc;
};
struct PhasedInputFailure {
  analysis::ExceptionInputFailure cause;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Keep binding/guard leaves visible while preserving resource classification.
struct PhasedModuleFailure {
  PhasedMethodError kind;
  std::variant<ModuleGuardError, ModuleAliasFailure> cause;
};
struct PhasedImportFailure {
  PhasedMethodError kind;
  analysis::ValueImportFailure cause;
  abc::EntityOffset method;
  std::optional<abc::CodeOffset> working_pc;
};
struct PhasedControlFailure {
  PhasedMethodError kind;
  std::variant<ExceptionControlError, ExceptionControlFailure, ExceptionPlanFailure> cause;
  abc::EntityOffset method;
  std::optional<abc::CodeOffset> pc, working_pc;
};
// Owns original exception analyses and the checked working body before child
// evaluation. Family discovery must inspect actual creation operations here,
// then discharge child/capture obligations before constructing source trees.
// This is NOT a source receipt or permission to invent FunctionBindings.
// The same-file occurrence/body contract matches PhasedMethod; views borrow
// this owner and the original body. Moving the owner preserves their storage.
// For normalized suspension input, the caller normalizes the decoded owner's
// async/generator protocol and supplies its exact await/yield/PC mapping.
// These spans are consumed during Build; provenance never grants semantics.
class PhasedBody {
 public:
  PhasedBody(PhasedBody&&) noexcept;
  PhasedBody& operator=(PhasedBody&&) noexcept;
  ~PhasedBody();
  [[nodiscard]] static std::variant<PhasedBody, PhasedMethodError, PhasedInputFailure, PhasedFlowFailure, PhasedModuleFailure, PhasedImportFailure, PhasedControlFailure> Build(
      const FunctionContext& context, const abc::MethodBody& body,
      analysis::SemanticBudget budget = {}, const PhasedModuleBinding* modules = nullptr,
      analysis::CapturedValues captures = analysis::CapturedValues::kReject,
      analysis::ClosureWrites writes = analysis::ClosureWrites::kUnknown,
      std::span<const analysis::AwaitSite> awaits = {},
      std::span<const abc::CodeOffset> source_pcs = {},
      std::span<const analysis::YieldSite> yields = {},
      std::span<const analysis::LanguageLoadFact> initialized_modules = {},
      const ModuleSlotSet* entry_modules = nullptr);
  const analysis::ValueProgram& Graph() const;
  const RouteInputIdentity& Identity() const;
  abc::EntityOffset Target() const;
  const ControlPlan& Control() const;
  const ModuleAliases* Modules() const;
  // Working topology/storage only, not initialization/lifetime permission.
  // Both are absent when no environment or creation operations need them.
  const analysis::ValueEnvironments* CellEnvironments() const;
  const CellPlan* Cells() const;
  ExceptionTreeInput TreeInput(const abc::MethodBody& body) const;
  std::size_t AllocatedBytes() const;
  std::uint64_t Work() const;
 private:
  struct Storage;
  explicit PhasedBody(std::unique_ptr<const Storage> storage);
  std::unique_ptr<const Storage> storage_;
};
}  // namespace abcd::source
#endif
