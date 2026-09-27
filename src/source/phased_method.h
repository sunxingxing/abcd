#ifndef ABCD_SOURCE_PHASED_METHOD_H_
#define ABCD_SOURCE_PHASED_METHOD_H_

#include "exception_tree.h"
#include "function_context.h"

namespace abcd::source {
struct PhasedModuleBinding;
struct ModuleAliases;
enum class PhasedMethodError : std::uint8_t {
  kInput, kSsa, kEnvironment, kValues, kImport, kRegion, kControl, kEvaluation,
  kResourceLimit, kWorkLimit, kContract, kModules
};
// Immutable owner for original phase analyses and their working control/value
// products. Views are assembled on demand: moving this owner never leaves
// references into moved stack objects. Same-file selection/body is a caller
// prerequisite; constants, ABI, captures and publication remain downstream.
class PhasedMethod {
 public:
  PhasedMethod(PhasedMethod&&) noexcept;
  PhasedMethod& operator=(PhasedMethod&&) noexcept;
  ~PhasedMethod();
  [[nodiscard]] static std::variant<PhasedMethod, PhasedMethodError> Build(
      const FunctionContext& context, const abc::MethodBody& body,
      analysis::SemanticBudget budget = {}, const PhasedModuleBinding* modules = nullptr);
  const analysis::ValueProgram& Graph() const;
  const EvaluationPlan& Evaluation() const;
  const ControlPlan& Control() const;
  const ModuleAliases* Modules() const;
  ExceptionTreeInput TreeInput(const abc::MethodBody& body) const;
  std::size_t AllocatedBytes() const;
  std::uint64_t Work() const;
 private:
  struct Storage;
  explicit PhasedMethod(std::unique_ptr<const Storage> storage);
  std::unique_ptr<const Storage> storage_;
};
}  // namespace abcd::source
#endif
