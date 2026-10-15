#ifndef ABCD_ABC_CLASS_KIND_H_
#define ABCD_ABC_CLASS_KIND_H_
#include <cstdint>
namespace abcd::abc {
// Pinned OH7 CreateClassWithBuffer / CreateSharedClass select different literal
// layouts and environment domains. The creating instruction owns this choice.
enum class ClassKind : std::uint8_t { kOrdinary, kSendable };
}
#endif
