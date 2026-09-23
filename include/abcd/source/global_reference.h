#ifndef ABCD_SOURCE_GLOBAL_REFERENCE_H_
#define ABCD_SOURCE_GLOBAL_REFERENCE_H_

#include <span>
#include <string_view>

namespace abcd::source {
struct GlobalScope {
  // Caller-proven complete ASCII outer source bindings, sorted by byte name;
  // non-ASCII names cannot collide with admitted ASCII global references.
  // Build once per record, never rescan per method.
  std::span<const std::string_view> enclosing_bindings;
  // Filled by the class emitter for its generated inner class expression.
  std::string_view class_binding;
  // Named function expressions bind their own name inside the body.
  std::string_view self_binding{};
  // Source formal names, sorted by byte name for O(log P) collision checks.
  std::span<const std::string_view> parameter_bindings{};
};

// A free identifier reaches the v7 global record/object only if the generated
// module/class wrapper cannot shadow it. Non-ASCII and strict-reserved names
// stay unsupported until source identifier handling proves their equivalence.
[[nodiscard]] bool AllowsGlobalReference(std::u16string_view name,
                                         GlobalScope scope);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_GLOBAL_REFERENCE_H_
