#ifndef ABCD_SOURCE_CLASS_SELF_ORDER_H_
#define ABCD_SOURCE_CLASS_SELF_ORDER_H_
#include "class_self.h"

namespace abcd::source {
struct FileFunction;
struct ClassInitializerFunction;
struct ClassInitializerWitness {
  ClassInitializerCall call;
  const ClassInitializerFunction& function;
};
enum class ClassSelfOrderError : std::uint8_t {
  kOwner, kInput, kObservation, kResourceLimit, kWorkLimit
};
// Same-file occurrence-bound evidence joining a private self-store interval
// with literal-only initialization of that fresh base class. The parent must
// remain unchanged and alive. Prototype spelling, the surrounding effect
// corridor and descendant capture publication remain separate obligations.
class ClassSelfOrder {
 public:
  ClassSelfOrder(ClassSelfOrder&&) noexcept = default;
  ClassSelfOrder& operator=(ClassSelfOrder&&) noexcept = default;
  static std::variant<ClassSelfOrder, ClassSelfOrderError> Build(const FileFunction&,
      std::span<const ClassInitializerWitness>, analysis::SemanticBudget = {});
  std::span<const ClassSelfStore> Stores(const analysis::ValueProgram& graph) const;
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  ClassSelfOrder() = default;
  std::vector<ClassSelfStore> stores_;
  const analysis::ValueOp* operations_ = nullptr;
  const analysis::ValueDefinition* values_ = nullptr;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_;
  std::size_t operation_count_ = 0, value_count_ = 0, bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
