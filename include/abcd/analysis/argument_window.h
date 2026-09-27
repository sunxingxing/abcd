#ifndef ABCD_ANALYSIS_ARGUMENT_WINDOW_H_
#define ABCD_ANALYSIS_ARGUMENT_WINDOW_H_

#include "abcd/abc/frame_layout.h"

namespace abcd::analysis {
// Invocation input, never inferred from declared formals or function.length.
// The callable owner must certify its provenance before source publication.
struct ActualArgumentCount {
  std::uint32_t value;
};
struct ArgumentPartition {
  std::uint32_t slots;
  std::uint32_t tail;
};
enum class ArgumentWindowError { kInvalidFrame, kMissingExtraArguments };

// Caller-certified invocation, not merely a layout. Under the existing legal
// JS entry contract its actual count and extra tail belong to this callable;
// the tail contains language values and cannot be addressed by declared vregs.
// Source owners must retain/recheck their creation identity. This fact does not
// authorize native JS arguments, arrow capture, or any source signature rewrite.
struct ArgumentFrameFact {
  abc::FrameLayout frame;
  bool operator==(const ArgumentFrameFact&) const = default;
};

// Pinned interpreter GetNumArgs(restIdx=0): current declared slots are a
// conditional prefix; extra arguments live outside the declared SSA frame.
// This validates layout, NOT invocation ownership, value domains or a profile.
// O(1) time/storage, no allocation even at the runtime's 28-bit count limit.
class ArgumentWindow {
 public:
  [[nodiscard]] static std::variant<ArgumentWindow, ArgumentWindowError> Build(
      const abc::FrameLayout& frame);
  [[nodiscard]] abc::RegisterId First() const { return first_; }
  [[nodiscard]] std::uint32_t Count() const { return count_; }
  // Declared may-read slot, not proof that this invocation supplied it.
  [[nodiscard]] std::optional<abc::RegisterId> At(std::uint32_t index) const;
  [[nodiscard]] ArgumentPartition Split(ActualArgumentCount actual) const;

 private:
  ArgumentWindow(abc::RegisterId first, std::uint32_t count)
      : first_(first), count_(count) {}
  abc::RegisterId first_;
  std::uint32_t count_;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ARGUMENT_WINDOW_H_
