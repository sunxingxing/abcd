#ifndef ABCD_SOURCE_LITERAL_CLASS_FIELDS_H_
#define ABCD_SOURCE_LITERAL_CLASS_FIELDS_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
enum class LiteralClassFieldError : std::uint8_t { kUnsupported, kFlow, kResourceLimit, kWorkLimit };
// Closed initializer effect certificate: one straight-line body, literal own
// writes through the entry receiver, then undefined return. The class owner
// must separately prove this receiver is its freshly created ordinary class;
// this certificate alone neither permits reordering nor excludes abrupt writes.
class LiteralClassFields {
 public:
  LiteralClassFields(LiteralClassFields&&) noexcept = default;
  LiteralClassFields& operator=(LiteralClassFields&&) noexcept = default;
  static std::variant<LiteralClassFields, LiteralClassFieldError> Build(
      const analysis::ValueProgram&, analysis::SemanticBudget = {});
  std::span<const analysis::OpId> Fields() const { return fields_; }
  analysis::IrRevision Revision() const { return revision_; }
  analysis::CfgRevision ControlRevision() const { return cfg_revision_; }
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  LiteralClassFields() = default;
  std::vector<analysis::OpId> fields_;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_revision_;
  std::size_t bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
