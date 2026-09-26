#ifndef ABCD_SOURCE_CREATION_EXPANSION_H_
#define ABCD_SOURCE_CREATION_EXPANSION_H_

#include <algorithm>
#include <array>
#include <optional>

#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
struct ClassMethodSelection;
enum class ExpansionError { kCycle, kResourceLimit };

// A synchronous emission tree, not a method scheduler. Depth bounds the host
// stack and ancestry scan. Scratch grants remain charged until the child dies;
// work grants are never refunded, including failed/fallback attempts.
class CreationExpansion {
 public:
  static constexpr std::size_t kMaxDepth = 32;
  CreationExpansion(abc::EntityOffset root, std::size_t bytes,
                    std::uint64_t work, const ClassMethodSelection* selection = nullptr)
      : bytes_(bytes), work_(work), selection_(selection) { active_[0] = root; }
  const ClassMethodSelection* Selection() const { return selection_; }
  CreationExpansion(const CreationExpansion&) = delete;
  CreationExpansion& operator=(const CreationExpansion&) = delete;

  std::optional<ExpansionError> Enter(abc::EntityOffset method,
                                      LocalLexicalBudget& budget) {
    for (std::size_t i = 0; i < depth_; ++i)
      if (active_[i] == method) return ExpansionError::kCycle;
    if (depth_ == kMaxDepth) return ExpansionError::kResourceLimit;
    // Reserve half for descendants/siblings rather than lend the same full
    // allowance to every live ancestor. These are policy limits, not ISA data.
    constexpr std::size_t kReserveDivisor = 2;
    budget.bytes = std::min(budget.bytes, bytes_ / kReserveDivisor);
    budget.work = std::min(budget.work, work_ / kReserveDivisor);
    budget.output.bytes = std::min(budget.output.bytes, budget.bytes);
    if (!budget.bytes || !budget.work || !budget.output.bytes)
      return ExpansionError::kResourceLimit;
    bytes_ -= budget.bytes;
    work_ -= budget.work;
    active_[depth_++] = method;
    return std::nullopt;
  }
  void Leave(std::size_t bytes) { --depth_; bytes_ += bytes; }

 private:
  std::array<abc::EntityOffset, kMaxDepth> active_{};
  std::size_t depth_ = 1;
  std::size_t bytes_;
  std::uint64_t work_;
  const ClassMethodSelection* selection_;
};

class ExpansionFrame {
 public:
  ExpansionFrame(CreationExpansion& expansion, abc::EntityOffset method,
                 LocalLexicalBudget& budget)
      : expansion_(expansion), error_(expansion.Enter(method, budget)),
        bytes_(budget.bytes) {}
  ~ExpansionFrame() { if (!error_) expansion_.Leave(bytes_); }
  ExpansionFrame(const ExpansionFrame&) = delete;
  ExpansionFrame& operator=(const ExpansionFrame&) = delete;
  std::optional<ExpansionError> error() const { return error_; }
 private:
  CreationExpansion& expansion_;
  std::optional<ExpansionError> error_;
  std::size_t bytes_;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_CREATION_EXPANSION_H_
