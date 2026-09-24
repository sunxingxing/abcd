#include "record_lexical.h"

#include <algorithm>
#include <limits>

#include "abcd/source/record_program.h"

namespace abcd::source {
namespace {
template <typename T>
void Reserve(std::vector<T>& values, std::size_t needed,
             std::size_t& bytes, abc::CodeOffset pc) {
  if (needed <= values.capacity()) return;
  const auto old = values.capacity();
  const auto available = bytes / sizeof(T);
  if (needed > available)
    throw RecordProgramFailure{RecordProgramError::kResourceLimit, pc};
  // Admission includes old and new allocations during vector growth. The old
  // allocation is already charged; credit it only after reserve releases it.
  const auto next = std::max(needed, old > available / 2 ? available : old * 2);
  values.reserve(next);
  if (values.capacity() > available)
    throw RecordProgramFailure{RecordProgramError::kResourceLimit, pc};
  bytes -= (values.capacity() - old) * sizeof(T);
}
}  // namespace

RecordValue& RecordLexicals::Cell(analysis::LexicalAddress address,
                                  abc::CodeOffset pc) {
  if (address.depth.value >= frames_.size())
    throw RecordProgramFailure{RecordProgramError::kValue, pc};
  const auto index = frames_.size() - 1 - address.depth.value;
  const auto base = frames_[index].base;
  const auto end = index + 1 < frames_.size() ? frames_[index + 1].base
                                             : cells_.size();
  if (address.slot.value >= end - base)
    throw RecordProgramFailure{RecordProgramError::kValue, pc};
  return cells_[base + address.slot.value];
}

bool RecordLexicals::Apply(const abc::BodyInstruction& site,
                            std::size_t frame_size,
                            RecordValue& accumulator, std::size_t& bytes) {
  const auto lifted = analysis::LiftLexicalInstruction(site.decoded, frame_size);
  if (const auto* error = std::get_if<analysis::LexicalError>(&lifted)) {
    if (*error == analysis::LexicalError::kUnsupportedOpcode) return false;
    throw RecordProgramFailure{RecordProgramError::kInstruction, site.offset};
  }
  if (const auto* push = std::get_if<analysis::PushEnvironment>(&lifted)) {
    if (push->slots > cells_.max_size() - cells_.size() ||
        frames_.size() == frames_.max_size() ||
        next_identity_ == std::numeric_limits<std::size_t>::max())
      throw RecordProgramFailure{RecordProgramError::kResourceLimit, site.offset};
    Reserve(frames_, frames_.size() + 1, bytes, site.offset);
    Reserve(cells_, cells_.size() + push->slots, bytes, site.offset);
    frames_.push_back({cells_.size(), ++next_identity_});
    cells_.resize(cells_.size() + push->slots, {RecordValue::Kind::kHole});
    accumulator = {RecordValue::Kind::kEnvironment, next_identity_};
  } else if (std::holds_alternative<analysis::PopEnvironment>(lifted)) {
    if (frames_.empty())
      throw RecordProgramFailure{RecordProgramError::kValue, site.offset};
    cells_.resize(frames_.back().base);
    frames_.pop_back();
  } else if (std::holds_alternative<analysis::CurrentEnvironment>(lifted)) {
    // Identity zero denotes the unknown incoming environment, never JS data.
    accumulator = {RecordValue::Kind::kEnvironment,
                   frames_.empty() ? 0 : frames_.back().identity};
  } else if (const auto* load = std::get_if<analysis::LexicalLoad>(&lifted)) {
    accumulator = Cell(load->address, site.offset);
  } else if (const auto* store = std::get_if<analysis::LexicalStore>(&lifted)) {
    if (!std::holds_alternative<analysis::Accumulator>(store->value))
      throw RecordProgramFailure{RecordProgramError::kInstruction, site.offset};
    Cell(store->address, site.offset) = accumulator;
  } else {
    throw RecordProgramFailure{RecordProgramError::kInstruction, site.offset};
  }
  return true;
}
}  // namespace abcd::source
