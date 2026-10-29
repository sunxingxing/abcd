#ifndef ABCD_SOURCE_CLASS_SELF_ORDER_H_
#define ABCD_SOURCE_CLASS_SELF_ORDER_H_
#include "abcd/analysis/semantic_validation.h"
#include "abcd/source/class_inner_binding.h"
#include "class_self.h"

namespace abcd::source {
struct FileFunction;
class ClassCreator;
class FunctionInput;
struct ClassInitializerFunction;
struct ClassSelfInitializationInput;
struct ClassSelfInitialization;
struct ClassSelfScope;
struct ClassInitializerWitness {
  ClassInitializerCall call;
  const ClassInitializerFunction& function;
};
enum class ClassSelfOrderError : std::uint8_t {
  kOwner, kInput, kObservation, kResourceLimit, kWorkLimit
};
// Same-file occurrence-bound evidence joining a private self-store interval
// with context-free initialization of that fresh base class. The parent must
// remain unchanged and alive. Prototype spelling, the surrounding effect
// corridor and descendant capture publication remain separate obligations.
class ClassSelfOrder {
 public:
  ClassSelfOrder(const ClassSelfOrder&) = default;
  ClassSelfOrder& operator=(const ClassSelfOrder&) = default;
  ClassSelfOrder(ClassSelfOrder&&) noexcept = default;
  ClassSelfOrder& operator=(ClassSelfOrder&&) noexcept = default;
  static std::variant<ClassSelfOrder, ClassSelfOrderError> Build(const FileFunction&,
      std::span<const ClassInitializerWitness>, analysis::SemanticBudget = {});
  // Same stores and witnesses for any creator. Phased creators (normal ==
  // nullptr) publish no LocalAccessPlan prefixes.
  static std::variant<ClassSelfOrder, ClassSelfOrderError> Build(const ClassCreator&,
      std::span<const ClassInitializerWitness>, analysis::SemanticBudget, const FileFunction* normal);
  static std::variant<ClassSelfOrder, ClassSelfOrderError> BindInitialization(
      const ClassSelfInitializationInput&, const ClassSelfInitialization&,
      const ClassSelfScope&, analysis::SemanticBudget = {});
  std::span<const ClassSelfStore> Stores(const analysis::ValueProgram& graph) const;
  std::span<const analysis::OpId> Installations(
      const analysis::ValueProgram& graph) const {
    return Stores(graph).empty() ? std::span<const analysis::OpId>{}
                                 : installations_;
  }
  std::span<const std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> Prefixes(
      const FunctionInput&, const analysis::ValueProgram&) const;
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  ClassSelfOrder() = default;
  std::vector<ClassSelfStore> stores_;
  std::vector<analysis::OpId> installations_;
  std::vector<std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> prefixes_;
  const analysis::SemanticInstruction* instructions_ = nullptr;
  const analysis::LocalCell* cells_ = nullptr;
  const analysis::ValueOp* operations_ = nullptr;
  const analysis::ValueDefinition* values_ = nullptr;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_;
  std::size_t operation_count_ = 0, value_count_ = 0, bytes_ = 0;
  std::uint64_t work_ = 0;
  std::optional<ClassInnerBinding> class_inner_;
};
}  // namespace abcd::source
#endif
