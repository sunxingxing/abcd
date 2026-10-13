#ifndef ABCD_SOURCE_PHASED_METHOD_H_
#define ABCD_SOURCE_PHASED_METHOD_H_

#include "phased_body.h"

namespace abcd::source {
// Immutable owner for original phase analyses and their working control/value
// products. Views are assembled on demand: moving this owner never leaves
// references into moved stack objects. Same-file selection/body is a caller
// prerequisite; constants, ABI, captures and publication remain downstream.
class PhasedMethod {
 public:
  PhasedMethod(PhasedMethod&&) noexcept;
  PhasedMethod& operator=(PhasedMethod&&) noexcept;
  ~PhasedMethod();
  [[nodiscard]] static std::variant<PhasedMethod, PhasedMethodError, EvaluationFailure> Build(
      const FunctionContext& context, const abc::MethodBody& body,
      analysis::SemanticBudget budget = {}, const PhasedModuleBinding* modules = nullptr);
  // Total allowance includes the body's retained storage and recorded work.
  // Missing child ownership still rejects; staged discovery cannot publish.
  [[nodiscard]] static std::variant<PhasedMethod, PhasedMethodError, EvaluationFailure> Evaluate(
      PhasedBody body, analysis::SemanticBudget budget = {});
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
