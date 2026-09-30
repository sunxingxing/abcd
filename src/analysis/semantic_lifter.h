#ifndef ABCD_ANALYSIS_SEMANTIC_LIFTER_H_
#define ABCD_ANALYSIS_SEMANTIC_LIFTER_H_

#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis::detail {
// One borrowed, monotone cursor for ordinary and exception CFG construction.
// Await/yield certificates come from a normalized owner, not opcode spelling.
// Call Complete after the last instruction; an unused certificate is invalid.
class SemanticLifter {
 public:
  SemanticLifter(abc::ProfileId profile, std::uint64_t frame_size,
                 std::span<const AwaitSite> awaits,
                 const abc::FrameLayout* frame = nullptr,
                 std::span<const YieldSite> yields = {})
      : profile_(profile), frame_size_(frame_size), awaits_(awaits), frame_(frame), yields_(yields) {}
  InstructionLift Lift(const abc::BodyInstruction& site);
  bool Complete() const {
    return next_await_ == awaits_.size() && next_mode_ == awaits_.size() &&
        next_yield_ == yields_.size() && next_yield_mode_ == yields_.size();
  }

 private:
  abc::ProfileId profile_;
  std::uint64_t frame_size_;
  std::span<const AwaitSite> awaits_;
  const abc::FrameLayout* frame_;
  std::size_t next_await_ = 0;
  std::size_t next_mode_ = 0;
  std::span<const YieldSite> yields_;
  std::size_t next_yield_ = 0, next_yield_mode_ = 0;
};
}  // namespace abcd::analysis::detail
#endif
