#ifndef ABCD_SOURCE_SCALAR_EXPRESSION_H_
#define ABCD_SOURCE_SCALAR_EXPRESSION_H_

#include <iosfwd>
#include <optional>
#include <span>
#include <string_view>

#include "abcd/abc/method_body.h"
#include "abcd/analysis/scalar.h"
#include "abcd/source/emission.h"

namespace abcd::source {
[[nodiscard]] bool IsSourceIdentifier(std::string_view name);
[[nodiscard]] bool IsSourceIdentifier(std::u16string_view name);
struct ScalarNames {
  struct RestBinding {
    std::uint16_t first_argument;
    std::string_view name;
    // Read-only printer substitution, never the original ABI argument slot.
    std::optional<abc::RegisterId> local{};
  };
  std::string_view accumulator;
  std::span<const std::string_view> registers;
  // Caller-proven immutable binding for the active function object. Empty
  // refuses LDFUNCTION; a writable VM function-slot register is not enough.
  std::string_view current_function{};
  std::optional<RestBinding> rest{};
  std::span<const abc::CodeOffset> constructor_this_writes{};
  // Set only after the complete derived-constructor super/guard proof.
  bool range_derived_constructor = false;
  // Owner-proven read-only ABI receiver in an ordinary non-derived callable.
  // Keep the register identifier for capture/legacy consumers; this typed
  // binding applies only to expression reads, never arbitrary identifier text.
  std::optional<abc::RegisterId> this_binding{};
  // Complete non-rest parameter may-write counts, indexed by formal ordinal.
  // Empty means no certificate. Borrowed from the callable owner; zero means
  // the corresponding ABI slot retains its entry value for the whole body.
  std::span<const std::int64_t> parameter_writes{};
  abc::RegisterId first_parameter{};
};
// Emits an expression, not a method/record. Referenced names must be declared,
// initialized, side-effect-free local bindings supplied by the caller. Only
// strict-safe ASCII identifiers are accepted for names; the separate typed
// this_binding certificate permits receiver reads. Nothing is silently renamed.
// Raw binary64 NaNs are rejected rather than discarding payload bits.
// O(referenced name bytes), fixed scratch; budget/validation failure writes
// nothing. A stream failure may leave partial output. Inputs must remain
// stable.
[[nodiscard]] std::variant<std::size_t, EmitError> WriteScalarExpression(
    const analysis::ScalarExpression& expression, ScalarNames names,
    std::ostream& output, EmitBudget budget = {});
}  // namespace abcd::source
#endif
