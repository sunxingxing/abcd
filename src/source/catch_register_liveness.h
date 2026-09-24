#ifndef ABCD_SOURCE_CATCH_REGISTER_LIVENESS_H_
#define ABCD_SOURCE_CATCH_REGISTER_LIVENESS_H_

#include <array>
#include <limits>
#include <vector>

#include "abcd/analysis/normal_flow.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
inline constexpr std::size_t kCatchTrackedRegisters = UINT8_MAX + 1;
inline constexpr std::size_t kCatchWordBits =
    std::numeric_limits<std::uint64_t>::digits;
inline constexpr std::size_t kCatchWords =
    (kCatchTrackedRegisters + kCatchWordBits - 1) / kCatchWordBits;
inline constexpr std::size_t kCatchMaxSuccessors =
    analysis::kMaxNormalSuccessors + 1;
// Exact backward may-liveness for the 256 v8-addressable registers, with a
// conservative exceptional edge from every try instruction to its catch-all.
// Each bit is added once; fixed lattice height makes work O(256 * (I + E)).
class CatchRegisterLiveness {
 public:
  struct Bits {
    std::array<std::uint64_t, kCatchWords> words{};
    [[nodiscard]] bool Has(abc::RegisterId reg) const;
  };
  [[nodiscard]] static std::variant<CatchRegisterLiveness, BodyEmitFailure>
  Build(const abc::MethodBody& body, std::size_t try_begin,
        std::size_t try_end, std::size_t handler_begin,
        std::uint64_t frame_size, std::size_t bytes, std::uint64_t work);
  [[nodiscard]] bool DeadAfter(std::size_t call, abc::RegisterId reg) const;
  [[nodiscard]] std::size_t AllocatedBytes() const;

 private:
  struct Edge {
    std::uint32_t index;
    bool exceptional = false;
  };
  struct Successors {
    std::array<Edge, kCatchMaxSuccessors> edges{};
    std::uint8_t count = 0;
  };
  CatchRegisterLiveness() = default;
  std::vector<Successors> successors_;
  std::vector<std::size_t> predecessor_starts_;
  std::vector<Edge> predecessors_;
  std::vector<Bits> uses_;
  std::vector<Bits> definitions_;
  std::vector<Bits> live_before_;
};
}  // namespace abcd::source

#endif  // ABCD_SOURCE_CATCH_REGISTER_LIVENESS_H_
