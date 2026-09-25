#include "parameter_aliases.h"

#include <algorithm>
#include <limits>

#include "abcd/abc/register_access.h"

namespace abcd::source {
bool FindParameterWrites(const abc::MethodBody& body, const abc::FrameLayout& frame,
                         std::span<std::int64_t> counts) {
  if (counts.size() != std::size_t(frame.parameters) + 1 ||
      frame.first_parameter.value > frame.registers ||
      frame.parameters > frame.registers - frame.first_parameter.value ||
      body.instructions.size() > std::numeric_limits<std::int64_t>::max() /
                                     abc::kMaxInstructionOperands) return false;
  std::fill(counts.begin(), counts.end(), 0);
  const std::uint64_t first = frame.first_parameter.value;
  const auto end = first + frame.parameters;
  for (const auto& site : body.instructions) {
    const auto accessed = abc::GetRegisterAccesses(site.decoded, frame.registers);
    const auto* known = std::get_if<abc::RegisterAccesses>(&accessed);
    if (!known) return false;
    for (std::size_t i = 0; i < known->count; ++i) {
      const auto& window = known->windows[i];
      if (window.access == abc::OperandAccess::kRead) continue;
      const auto begin = std::max(first, std::uint64_t(window.first.value));
      const auto limit = std::min(end, std::uint64_t(window.first.value) + window.count);
      if (begin >= limit) continue;
      ++counts[begin - first];
      --counts[limit - first];
    }
  }
  std::int64_t active = 0;
  for (std::size_t i = 0; i < frame.parameters; ++i) {
    active += counts[i];
    counts[i] = active;
  }
  return true;
}
}  // namespace abcd::source
