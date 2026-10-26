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
#include "abcd/analysis/capture_effect.h"
#include "abcd/analysis/phi_placement.h"
#include "abcd/analysis/register_ssa.h"

namespace abcd::analysis {
struct SuperForward {
  abc::RegisterId constructor;
};
enum class SuperCalleeSource : std::uint8_t { kInvocationFunction, kAccumulator };
struct SuperRange {
  abc::RegisterId first;
  std::uint16_t count;
  // Narrow form's IC metadata is retained but never printed as an argument.
  std::optional<std::uint16_t> cache_slot;
  SuperCalleeSource callee = SuperCalleeSource::kInvocationFunction;
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

enum class ReceiverState : std::uint8_t {
  kUnreachable = 0,
  kUninitialized = 1,
  kInitialized = 2,
  kBoth = 3
};
enum class DerivedCheckAction : std::uint8_t { kNoop, kBind, kRead };
struct DerivedSuperSite {
  InstructionId instruction;
  abc::CodeOffset pc;
  bool operator==(const DerivedSuperSite&) const = default;
};
struct DerivedReturnSite {
  InstructionId instruction;
  ReceiverState receiver;
  bool operator==(const DerivedReturnSite&) const = default;
};
struct DerivedCoverage {
  InstructionId instruction;
  DerivedCheckAction action;
  std::optional<InstructionId> construction;
  bool operator==(const DerivedCoverage&) const = default;
};
struct DerivedBindingSite {
  InstructionId guard, construction;
  bool operator==(const DerivedBindingSite&) const = default;
};
struct RangeDerivedConstructor {
  std::vector<DerivedSuperSite> sites;
  std::vector<DerivedReturnSite> returns;
  std::vector<DerivedCoverage> coverage;
  std::vector<DerivedBindingSite> bindings;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
using RangeDerivedConstructorResult =
    std::variant<RangeDerivedConstructor, DerivedConstructorError>;
// Range protocols retain every call/return/check. The historical harmless-
// guard proof remains a sufficient fast path, preserving existing projections.
// The general proof compares actual SSA receiver values with an invocation-
// owned shadow binding and tracks initialization on normal CFG paths. Native
// bind/read/exit checks have explicit coverage, including throwing paths.
// Copies, phis and block splitting do not change the protocol. Remaining
// exception regions require the existing transparent-rethrow normalization;
// arbitrary handlers and unproved lexical receiver effects remain refused.
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
