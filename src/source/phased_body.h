#ifndef ABCD_SOURCE_PHASED_BODY_H_
#define ABCD_SOURCE_PHASED_BODY_H_

#include "exception_tree.h"
#include "function_context.h"

namespace abcd::source {
struct PhasedModuleBinding;
struct ModuleAliases;
enum class PhasedMethodError : std::uint8_t {
  kInput, kSsa, kEnvironment, kValues, kImport, kRegion, kControl, kEvaluation,
  kResourceLimit, kWorkLimit, kContract, kModules, kCells
};
// Owns original exception analyses and the checked working body before child
// evaluation. Family discovery must inspect actual creation operations here,
// then discharge child/capture obligations before constructing source trees.
// This is NOT a source receipt or permission to invent FunctionBindings.
// The same-file occurrence/body contract matches PhasedMethod; views borrow
// this owner and the original body. Moving the owner preserves their storage.
class PhasedBody {
 public:
  PhasedBody(PhasedBody&&) noexcept;
  PhasedBody& operator=(PhasedBody&&) noexcept;
  ~PhasedBody();
  [[nodiscard]] static std::variant<PhasedBody, PhasedMethodError> Build(
      const FunctionContext& context, const abc::MethodBody& body,
      analysis::SemanticBudget budget = {}, const PhasedModuleBinding* modules = nullptr);
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
