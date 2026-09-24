#ifndef ABCD_SOURCE_CONSTRUCTOR_FIELDS_H_
#define ABCD_SOURCE_CONSTRUCTOR_FIELDS_H_

#include <cstddef>
#include <vector>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"

namespace abcd::source {
struct ConstructorThisWrites {
  std::vector<abc::CodeOffset> sites;
  [[nodiscard]] std::size_t AllocatedBytes() const {
    return sites.capacity() * sizeof(abc::CodeOffset);
  }
};

// Only exact local copies of the base constructor's ABI this slot qualify.
// Unknown effects stop the proof; a false negative retains the original
// register-based write. No field is moved ahead of its bytecode site.
[[nodiscard]] ConstructorThisWrites ProveConstructorThisWrites(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    std::size_t bytes);
}  // namespace abcd::source

#endif
