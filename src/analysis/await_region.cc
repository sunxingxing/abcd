#include "abcd/analysis/await_region.h"
#include "abcd/abc/profile_capabilities.h"

#include <algorithm>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/analysis/register_effect.h"

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
// The continuation runs after the removed protocol, where the VM accumulator
// still holds the mode comparison. It may overwrite, never read, that value.
bool ReadsAccumulator(const abc::BodyInstruction& site, std::uint64_t frame_size) {
  const auto lifted = LiftRegisterEffect(site.decoded, frame_size);
  const auto* effect = std::get_if<RegisterEffect>(&lifted);
  if (!effect) return true;
  for (std::uint8_t i = 0; i < effect->read_count; ++i)
    if (std::holds_alternative<Accumulator>(effect->reads[i])) return true;
  return false;
}
struct Matched {
  AwaitRegion region;
  // Set when the THROW constant is read from a register. The caller must
  // prove that register holds exactly that integer on every normal path.
  std::optional<abc::RegisterId> throw_mode;
};
std::optional<Matched> Match(std::span<const abc::BodyInstruction> sites,
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
      ReadsAccumulator(sites[kContinue], frame_size) ||
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
  const auto* constant = value ? std::get_if<abc::RegisterId>(value) : nullptr;
  const auto* binary = std::get_if<BinaryExpression>(&equal->expression);
  if ((!constant && (!integer || integer->value != kThrowResumeMode)) || !binary ||
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
  return Matched{region, constant ? std::optional{*constant} : std::nullopt};
}
// Optimized code may reuse a register already holding the THROW constant.
// Forward must-analysis over normal and resume edges (suspension preserves
// registers): a register is "one" only if every path last wrote it from the
// integer literal. Handler entries and unknown writers are top-free (zero).
// Tracks at most 64 candidate registers; each block state only loses bits,
// so every block is re-queued at most 65 times.
struct ConstantObligation { std::size_t instruction; abc::RegisterId slot; };
bool ProveThrowModeRegisters(const abc::MethodBody& body, std::uint64_t frame_size,
                             std::span<const ConstantObligation> obligations,
                             std::size_t bytes) {
  std::vector<abc::RegisterId> candidates;
  for (const auto& obligation : obligations)
    if (std::find(candidates.begin(), candidates.end(), obligation.slot) == candidates.end())
      candidates.push_back(obligation.slot);
  if (candidates.size() > 63) return false;
  const auto bit = [&](abc::RegisterId slot) -> std::uint64_t {
    const auto at = std::find(candidates.begin(), candidates.end(), slot);
    return at == candidates.end() ? 0 : std::uint64_t{1} << (at - candidates.begin());
  };
  constexpr std::uint64_t kAccumulator = std::uint64_t{1} << 63;
  const auto flow = BuildNormalFlow(body, {bytes});
  const auto* graph = std::get_if<NormalFlow>(&flow);
  if (!graph || graph->blocks.size() > bytes / (2 * sizeof(std::uint64_t))) return false;
  const auto transfer = [&](std::uint64_t state, const abc::BodyInstruction& site) {
    const auto lifted = LiftScalarInstruction(site.decoded, frame_size);
    if (const auto* assignment = std::get_if<ScalarAssignment>(&lifted)) {
      const auto* value = std::get_if<ScalarValue>(&assignment->expression);
      std::uint64_t one = 0;
      if (value) {
        if (const auto* integer = std::get_if<IntegerLiteral>(value))
          one = integer->value == kThrowResumeMode;
        else if (std::holds_alternative<Accumulator>(*value))
          one = (state & kAccumulator) != 0;
        else if (const auto* slot = std::get_if<abc::RegisterId>(value))
          one = (state & bit(*slot)) != 0;
      }
      const auto target = std::holds_alternative<Accumulator>(assignment->destination)
          ? kAccumulator : bit(std::get<abc::RegisterId>(assignment->destination));
      return one ? state | target : state & ~target;
    }
    const auto effect = LiftRegisterEffect(site.decoded, frame_size);
    const auto* write = std::get_if<RegisterEffect>(&effect);
    if (!write) return std::uint64_t{0};
    if (!write->write) return state;
    return state & ~(std::holds_alternative<Accumulator>(*write->write)
        ? kAccumulator : bit(std::get<abc::RegisterId>(*write->write)));
  };
  const auto& blocks = graph->blocks;
  constexpr std::uint64_t kTop = ~std::uint64_t{0};
  std::vector<std::uint64_t> entry(blocks.size(), kTop);
  std::vector<std::uint8_t> queued(blocks.size(), 0);
  std::vector<std::uint32_t> work;
  const auto seed = [&](std::size_t block, std::uint64_t state) {
    const auto merged = entry[block] & state;
    if (merged == entry[block] && entry[block] != kTop) return;
    entry[block] = merged;
    if (!queued[block]) { queued[block] = 1; work.push_back(static_cast<std::uint32_t>(block)); }
  };
  seed(0, 0);
  for (const auto& handler : body.handlers)
    for (std::size_t b = 0; b < blocks.size(); ++b)
      if (blocks[b].begin == handler.offset) seed(b, 0);
  std::uint64_t visits = 0;
  while (!work.empty()) {
    const auto b = work.back(); work.pop_back(); queued[b] = 0;
    if (++visits > 65 * blocks.size() + 1) return false;
    auto state = entry[b];
    const auto& block = blocks[b];
    for (std::uint32_t i = 0; i < block.instruction_count; ++i)
      state = transfer(state, body.instructions[block.first_instruction + i]);
    for (std::uint8_t e = 0; e < block.successor_count; ++e)
      seed(block.successors[e].target.value, state);
  }
  for (const auto& obligation : obligations) {
    const auto at = std::find_if(blocks.begin(), blocks.end(), [&](const FlowBlock& block) {
      return obligation.instruction >= block.first_instruction &&
             obligation.instruction < block.first_instruction + block.instruction_count;
    });
    if (at == blocks.end() || entry[at - blocks.begin()] == kTop) return false;
    auto state = entry[at - blocks.begin()];
    for (auto i = at->first_instruction; i < obligation.instruction; ++i)
      state = transfer(state, body.instructions[i]);
    if (!(state & bit(obligation.slot))) return false;
  }
  return true;
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
    std::vector<ConstantObligation> obligations;
    std::vector<abc::CodeOffset> obligation_awaits;
    regions.reserve(count);
    if (regions.capacity() > bytes / sizeof(AwaitRegion))
      return AwaitRegionFailure{AwaitRegionError::kResourceLimit, {}};
    for (std::size_t i = 0; i < sites.size(); ++i) {
      const auto lifted = LiftCoroutineInstruction(sites[i].decoded, frame_size);
      const auto* transfer = std::get_if<CoroutineTransfer>(&lifted);
      if (!transfer || transfer->kind != CoroutineTransferKind::kAwait) continue;
      const auto matched = Match(sites.subspan(i), frame_size);
      if (!matched)
        return AwaitRegionFailure{AwaitRegionError::kUnsupportedShape, sites[i].offset};
      regions.push_back(matched->region);
      if (matched->throw_mode) {
        obligations.push_back({i + kThrowMode, *matched->throw_mode});
        obligation_awaits.push_back(sites[i].offset);
      }
      i += kContinue - 1;
    }
    if (!obligations.empty() &&
        !ProveThrowModeRegisters(body, frame_size, obligations, bytes))
      return AwaitRegionFailure{AwaitRegionError::kUnsupportedShape, obligation_awaits.front()};
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
