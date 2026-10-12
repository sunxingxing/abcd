#include "abcd/analysis/await_region.h"
#include "abcd/abc/profile_capabilities.h"

#include <algorithm>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/coroutine.h"

namespace abcd::analysis {
namespace {
// Pinned js_generator_object.h GeneratorResumeMode::THROW. Never infer this
// value from application literals; it is an explicit runtime ABI constant.
constexpr auto kThrowResumeMode =
    static_cast<std::int32_t>(CoroutineResumeMode::kThrow);
enum AwaitStep : std::size_t {
  kAwait, kSuspend, kLoadState, kResume, kSaveResult, kReloadState,
  kReadMode, kSaveMode, kThrowMode, kCompareMode, kBranch,
  kLoadRejected, kThrow, kContinue, kStepCount
};
static_assert(kStepCount == kAwaitProtocolInstructions);

std::optional<abc::RegisterId> CopyRegister(
    const abc::BodyInstruction& site, std::uint64_t frame_size, bool store) {
  const auto lifted = LiftScalarInstruction(site.decoded, frame_size);
  const auto* assignment = std::get_if<ScalarAssignment>(&lifted);
  if (!assignment) return std::nullopt;
  const auto* value = std::get_if<ScalarValue>(&assignment->expression);
  if (!value) return std::nullopt;
  if (store && std::holds_alternative<Accumulator>(*value)) {
    if (const auto* reg = std::get_if<abc::RegisterId>(&assignment->destination))
      return *reg;
  }
  if (!store &&
      std::holds_alternative<Accumulator>(assignment->destination)) {
    if (const auto* reg = std::get_if<abc::RegisterId>(value)) return *reg;
  }
  return std::nullopt;
}
bool ReadsState(const abc::BodyInstruction& site, std::uint64_t frame_size,
                CoroutineReadKind kind) {
  const auto lifted = LiftCoroutineInstruction(site.decoded, frame_size);
  const auto* read = std::get_if<CoroutineRead>(&lifted);
  return read && read->kind == kind &&
         std::holds_alternative<Accumulator>(read->state);
}
std::optional<AwaitRegion> Match(std::span<const abc::BodyInstruction> sites,
                                 std::uint64_t frame_size) {
  if (sites.size() < kStepCount) return std::nullopt;
  const auto awaited = LiftCoroutineInstruction(sites[kAwait].decoded, frame_size);
  const auto suspended = LiftCoroutineInstruction(sites[kSuspend].decoded, frame_size);
  const auto* wait = std::get_if<CoroutineTransfer>(&awaited);
  const auto* suspend = std::get_if<CoroutineTransfer>(&suspended);
  if (!wait || !suspend || wait->kind != CoroutineTransferKind::kAwait ||
      suspend->kind != CoroutineTransferKind::kSuspend ||
      !std::holds_alternative<Accumulator>(wait->value) ||
      !std::holds_alternative<Accumulator>(suspend->value))
    return std::nullopt;
  const auto* state = std::get_if<abc::RegisterId>(&wait->state);
  const auto* suspended_state = std::get_if<abc::RegisterId>(&suspend->state);
  const auto result = CopyRegister(sites[kSaveResult], frame_size, true);
  const auto mode = CopyRegister(sites[kSaveMode], frame_size, true);
  if (!state || !suspended_state || *state != *suspended_state ||
      !result || !mode || *result == *state || *mode == *state || *mode == *result ||
      CopyRegister(sites[kLoadState], frame_size, false) != *state ||
      CopyRegister(sites[kReloadState], frame_size, false) != *state ||
      CopyRegister(sites[kLoadRejected], frame_size, false) != *result ||
      CopyRegister(sites[kContinue], frame_size, false) != *result ||
      !ReadsState(sites[kResume], frame_size, CoroutineReadKind::kResumeValue) ||
      !ReadsState(sites[kReadMode], frame_size, CoroutineReadKind::kResumeMode))
    return std::nullopt;
  const auto literal = LiftScalarInstruction(sites[kThrowMode].decoded, frame_size);
  const auto compare = LiftScalarInstruction(sites[kCompareMode].decoded, frame_size);
  const auto* load = std::get_if<ScalarAssignment>(&literal);
  const auto* equal = std::get_if<ScalarAssignment>(&compare);
  if (!load || !equal ||
      !std::holds_alternative<Accumulator>(load->destination) ||
      !std::holds_alternative<Accumulator>(equal->destination))
    return std::nullopt;
  const auto* value = std::get_if<ScalarValue>(&load->expression);
  const auto* integer = value ? std::get_if<IntegerLiteral>(value) : nullptr;
  const auto* binary = std::get_if<BinaryExpression>(&equal->expression);
  if (!integer || integer->value != kThrowResumeMode || !binary ||
      binary->op != BinaryOperator::kEqual ||
      !std::holds_alternative<Accumulator>(binary->right))
    return std::nullopt;
  const auto* compared_mode = std::get_if<abc::RegisterId>(&binary->left);
  if (!compared_mode || *compared_mode != *mode) return std::nullopt;
  const auto condition = LiftBranchCondition(sites[kBranch].decoded);
  const auto* predicate = std::get_if<UnaryExpression>(&condition);
  if (!predicate || predicate->op != UnaryOperator::kFalseOrZero ||
      sites[kBranch].branch_target != sites[kContinue].offset ||
      sites[kThrow].decoded.opcode != abc::Opcode::kTHROW_PREF_NONE ||
      sites[kThrow].decoded.operand_count != 0)
    return std::nullopt;
  AwaitRegion region{sites[kAwait].offset, sites[kSuspend].offset,
                     sites[kResume].offset, sites[kSaveResult].offset,
                     sites[kReadMode].offset, sites[kSaveMode].offset,
                     sites[kBranch].offset, sites[kThrow].offset,
                     sites[kContinue].offset, *state, *result, *mode};
  region.instructions.emplace();
  for (std::size_t i = 0; i < kStepCount; ++i)
    (*region.instructions)[i] = sites[i].offset;
  return region;
}
// Strict interior excludes the continuation, whose original load is retained.
const AwaitRegion* Interior(const std::vector<AwaitRegion>& regions,
                            std::uint64_t pc) {
  auto it = std::lower_bound(regions.begin(), regions.end(), pc,
      [](const AwaitRegion& region, std::uint64_t value) {
        return region.await.value < value;
      });
  if (it == regions.begin()) return nullptr;
  --it;
  return pc < it->continuation.value ? &*it : nullptr;
}
}  // namespace

AwaitRegions FindAwaitRegions(const abc::MethodBody& body, abc::ProfileId profile,
                             std::uint64_t frame_size, std::size_t bytes) {
  if (!abc::HasPinnedRuntimeSemantics(profile))
    return AwaitRegionFailure{AwaitRegionError::kUnsupportedProfile, {}};
  try {
    const auto sites = std::span(body.instructions);
    std::size_t count = 0;
    for (const auto& site : sites) {
      const auto lifted = LiftCoroutineInstruction(site.decoded, frame_size);
      if (const auto* transfer = std::get_if<CoroutineTransfer>(&lifted);
          transfer && transfer->kind == CoroutineTransferKind::kAwait)
        ++count;
    }
    if (count > bytes / sizeof(AwaitRegion))
      return AwaitRegionFailure{AwaitRegionError::kResourceLimit, {}};
    std::vector<AwaitRegion> regions;
    regions.reserve(count);
    if (regions.capacity() > bytes / sizeof(AwaitRegion))
      return AwaitRegionFailure{AwaitRegionError::kResourceLimit, {}};
    for (std::size_t i = 0; i < sites.size(); ++i) {
      const auto lifted = LiftCoroutineInstruction(sites[i].decoded, frame_size);
      const auto* transfer = std::get_if<CoroutineTransfer>(&lifted);
      if (!transfer || transfer->kind != CoroutineTransferKind::kAwait) continue;
      const auto region = Match(sites.subspan(i), frame_size);
      if (!region)
        return AwaitRegionFailure{AwaitRegionError::kUnsupportedShape, sites[i].offset};
      regions.push_back(*region);
      i += kContinue - 1;
    }
    for (const auto& site : sites) {
      if (site.branch_target && Interior(regions, site.branch_target->value))
        return AwaitRegionFailure{AwaitRegionError::kInteriorEntry, site.offset};
    }
    for (const auto& region : body.tries) {
      if (Interior(regions, region.offset.value) ||
          Interior(regions, std::uint64_t(region.offset.value) + region.length))
        return AwaitRegionFailure{AwaitRegionError::kExceptionBoundary, region.offset};
    }
    for (const auto& handler : body.handlers) {
      if (Interior(regions, handler.offset.value) ||
          Interior(regions, std::uint64_t(handler.offset.value) + handler.length))
        return AwaitRegionFailure{AwaitRegionError::kExceptionBoundary, handler.offset};
    }
    return regions;
  } catch (const std::bad_alloc&) {
    return AwaitRegionFailure{AwaitRegionError::kResourceLimit, {}};
  } catch (const std::length_error&) {
    return AwaitRegionFailure{AwaitRegionError::kResourceLimit, {}};
  }
}
}  // namespace abcd::analysis
