#ifndef ABCD_ANALYSIS_PROPERTY_RECEIVER_H_
#define ABCD_ANALYSIS_PROPERTY_RECEIVER_H_

#include "abcd/abc/method_body.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
enum class ReceiverOrigin : std::uint8_t { kUnknown, kEntryThis };
struct PropertyReceiver {
  abc::CodeOffset pc;
  ReceiverOrigin origin;
};
enum class ReceiverError { kFrameRange, kMalformedInstruction, kResourceLimit };
struct ReceiverBudget {
  std::size_t bytes = 16 * 1024 * 1024;
};
// Requires DecodeMethodBody output. Proves only normal-entry prefix identities:
// stop before any incoming branch/handler target, non-fallthrough or
// unsupported operation. Unknown is NOT distinct-from-this. Entry this may be a
// Hole in a derived constructor: this API does not prove an instance or move
// field writes. O(instructions + registers), O(registers + property writes),
// bounded extra storage. entry_this names an optional ABI register copy. LDTHIS
// reads the independent frame.thisObj, including when that register is absent
// or has been overwritten.
[[nodiscard]] std::variant<std::vector<PropertyReceiver>, ReceiverError>
ProvePropertyReceivers(const abc::MethodBody& body, std::uint32_t registers,
                       std::optional<abc::RegisterId> entry_this,
                       ReceiverBudget budget = {});
}  // namespace abcd::analysis
#endif
