#ifndef ABCD_ANALYSIS_DERIVED_CONSTRUCTOR_H_
#define ABCD_ANALYSIS_DERIVED_CONSTRUCTOR_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/phi_placement.h"
#include "abcd/analysis/capture_effect.h"

namespace abcd::analysis {
struct SuperForward {
  abc::RegisterId constructor;
};
struct SuperRange {
  abc::RegisterId first;
  std::uint16_t count;
  // Narrow form's IC metadata is retained but never printed as an argument.
  std::optional<std::uint16_t> cache_slot;
};
struct SuperSpread {
  abc::RegisterId arguments;
  std::uint8_t cache_slot;
};
enum class SuperCheckKind {
  kRequireInitialized,
  kRequireUninitialized,
  kNoop
};
struct SuperCheck {
  SuperCheckKind kind;
};
enum class SuperError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using SuperInstruction =
    std::variant<SuperForward, SuperRange, SuperSpread, SuperCheck, SuperError>;
[[nodiscard]] SuperInstruction LiftSuperInstruction(
    const abc::DecodedInstruction& instruction, std::uint32_t registers);

enum class ForwardingKind { kAllArguments, kParameters };
struct ForwardingDerivedConstructor {
  ForwardingKind kind = ForwardingKind::kAllArguments;
  std::uint16_t argument_count = 0;
};
enum class DerivedConstructorError {
  kFrame,
  kControlFlow,
  kInstruction,
  kValue,
  kGuard,
  kReturn,
  kResourceLimit
};
struct DerivedConstructorBudget {
  static constexpr std::size_t kDefaultBytes = 16 * 1024 * 1024;
  static constexpr std::uint64_t kDefaultWork = std::numeric_limits<std::uint64_t>::max();
  std::size_t bytes = kDefaultBytes;
  std::uint64_t work = kDefaultWork;
};
using DerivedConstructorResult = std::variant<ForwardingDerivedConstructor,
                                              DerivedConstructorError>;
// Pinned v7 constructor-template proof, O(instructions + frame registers).
// The caller must separately prove derived class creation, parent identity,
// method target/ABI and observable length before emitting JS `super(...)` or
// an explicit ordered parameter list. kParameters proves each range value is
// the corresponding ABI parameter, even after pure local copies.
// No side-effecting operation or write to an ABI input slot is admitted.
[[nodiscard]] DerivedConstructorResult ProveForwardingDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget = {});

struct StringFieldWrite {
  abc::PoolIndex name;
  abc::PoolIndex value;
};
struct SpreadFieldDerivedConstructor {
  std::vector<StringFieldWrite> writes;
};
using SpreadFieldConstructorResult =
    std::variant<SpreadFieldDerivedConstructor, DerivedConstructorError>;
// Exact pinned-v7 rest-spread constructor template. The proof preserves
// ordered strict setter effects; it never reclassifies them as field defines.
// An empty writes vector admits the default constructor protocol, not a
// parent-brand invariant or permission to omit the explicit spread operation.
// O(instructions + registers), bounded by the caller's byte admission.
[[nodiscard]] SpreadFieldConstructorResult ProveSpreadFieldDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget = {});

struct RangeDerivedConstructor {
  abc::CodeOffset super_pc;
};
using RangeDerivedConstructorResult =
    std::variant<RangeDerivedConstructor, DerivedConstructorError>;
// A single range super call must dominate every normal return. Sparse SSA
// origin propagation proves each VM super check is a no-op on its actual
// input; arbitrary constructors and inferred source types are not admitted.
// Local stores/loads participate through sparse cell SSA before the first
// capture-capable creation not covered by a same-body preservation certificate.
// Later cell loads remain unknown. The default
// profile is the pinned v7 dynamic 13.0.1 contract; file owners pass theirs.
// CFG successor reachability proves the call cannot repeat. Independent loops
// and backward physical layouts are admitted; guard count/placement do not
// define initialization. Exception regions remain outside this normal proof.
[[nodiscard]] RangeDerivedConstructorResult ProveRangeDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget = {},
    abc::ProfileId profile = abc::ProfileId::kDynamic1301,
    std::span<const CapturePreservation> preserved = {});
struct SpreadDerivedConstructor {
  abc::CodeOffset super_pc;
};
using SpreadDerivedConstructorResult =
    std::variant<SpreadDerivedConstructor, DerivedConstructorError>;
// Same sparse normal-flow initialization proof as range calls, additionally
// proving the explicit acc operand is this invocation's current constructor.
// Does not authorize native spread: argument-array expansion, parent runtime
// brand, newTarget and source parameter recovery retain their own obligations.
[[nodiscard]] SpreadDerivedConstructorResult ProveSpreadDerivedConstructor(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget = {},
    abc::ProfileId profile = abc::ProfileId::kDynamic1301,
    std::span<const CapturePreservation> preserved = {});
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_DERIVED_CONSTRUCTOR_H_
