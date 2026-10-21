#ifndef ABCD_SOURCE_CLOSED_CLASS_FIELDS_H_
#define ABCD_SOURCE_CLOSED_CLASS_FIELDS_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
enum class ClosedClassFieldError : std::uint8_t { kUnsupported, kFlow, kResourceLimit, kWorkLimit };
struct ClosedClassFieldCost { std::size_t bytes = 0; std::uint64_t work = 0; };
// Closed initializer effect certificate: all paths execute one ordered chain of
// closed own writes through proven receiver sources, then undefined return. CFG
// placement is irrelevant; divergence, bypass and repeated writes fail. The class owner
// must separately prove this receiver is its freshly created ordinary class;
// this certificate alone neither permits reordering nor excludes abrupt writes.
class ClosedClassFields {
 public:
  ClosedClassFields(ClosedClassFields&&) noexcept = default;
  ClosedClassFields& operator=(ClosedClassFields&&) noexcept = default;
  static std::variant<ClosedClassFields, ClosedClassFieldError> Build(
      const analysis::ValueProgram&, analysis::SemanticBudget = {},
      ClosedClassFieldCost* attempted = nullptr);
  std::span<const analysis::OpId> Fields() const { return fields_; }
  analysis::IrRevision Revision() const { return revision_; }
  analysis::CfgRevision ControlRevision() const { return cfg_revision_; }
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  ClosedClassFields() = default;
  std::vector<analysis::OpId> fields_;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_revision_;
  std::size_t bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
