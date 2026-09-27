#include "abcd/analysis/argument_window.h"

#include <algorithm>
#include <limits>

namespace abcd::analysis {
std::variant<ArgumentWindow, ArgumentWindowError> ArgumentWindow::Build(
    const abc::FrameLayout& frame) {
  const abc::CallFlags flags{frame.this_value.has_value(),
      frame.new_target.has_value(), frame.extra_arguments,
      frame.function.has_value()};
  const auto hidden = std::uint32_t(flags.function_slot) +
      flags.new_target_slot + flags.this_slot;
  if (frame.parameters > std::numeric_limits<std::uint32_t>::max() - hidden)
    return ArgumentWindowError::kInvalidFrame;
  // Reuse the pinned width/order authority. Merely checking the total frame
  // size would accept aliased hidden slots or a displaced argument prefix.
  const auto rebuilt = abc::BuildFrameLayout(
      frame.locals, frame.parameters + hidden, flags);
  const auto* canonical = std::get_if<abc::FrameLayout>(&rebuilt);
  if (!canonical || *canonical != frame)
    return ArgumentWindowError::kInvalidFrame;
  if (!frame.extra_arguments)
    return ArgumentWindowError::kMissingExtraArguments;
  return ArgumentWindow{frame.first_parameter, frame.parameters};
}

std::optional<abc::RegisterId> ArgumentWindow::At(std::uint32_t index) const {
  if (index >= count_) return {};
  return abc::RegisterId{first_.value + index};
}

ArgumentPartition ArgumentWindow::Split(ActualArgumentCount actual) const {
  const auto slots = std::min(actual.value, count_);
  return {slots, actual.value - slots};
}
}  // namespace abcd::analysis
