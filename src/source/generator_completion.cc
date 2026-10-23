#include "generator_completion.h"
#include "abcd/abc/profile_capabilities.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/await_catch.h"
#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/normal_flow.h"
#include "async_handlers.h"
#include "generator_async_yield.h"

namespace abcd::source {
namespace {
using abc::Opcode;
using Error = FunctionEmitError;
void Require(bool value) { if (!value) throw Error::kKind; }
bool Op(const abc::BodyInstruction& site, Opcode op) { return site.decoded.opcode == op; }
abc::RegisterId Reg(const abc::BodyInstruction& site, std::size_t operand = 0) {
  Require(operand < site.decoded.operand_count &&
      site.decoded.operands[operand].kind == abc::OperandKind::kRegister &&
      site.decoded.operands[operand].raw_bits <= std::numeric_limits<std::uint32_t>::max());
  return {static_cast<std::uint32_t>(site.decoded.operands[operand].raw_bits)};
}
// Schema indices are instruction positions, not byte widths. Pinned:
// FunctionBuilder::{SuspendResumeExecution,HandleCompletion}, OH7 frontend.
enum ResumeStep : std::size_t {
  kSuspend, kStateForValue, kResume, kStoreResult, kStateForMode, kMode, kStoreMode,
  kReturnMode, kReturnEqual, kReturnBranch, kReturnValue, kReturn,
  kThrowMode, kThrowEqual, kThrowBranch, kThrowValue, kThrow, kContinuation, kResumeSize
};
constexpr auto kPreservingTailSize = kReturn - kReturnValue + 1;
// Pinned AsyncGeneratorFunctionBuilder::{ImplicitReturn,DirectReturn,CleanUp}:
// instruction positions, not encoded byte widths or guessed source returns.
enum AsyncExitStep : std::size_t {
  kImplicitValue, kSaveExitValue, kCanSuspend, kSaveCanSuspend, kExitState,
  kExitComplete, kResolveExit, kExitReturn, kAsyncExitSize
};
enum AsyncCatchStep : std::size_t {
  kSaveException, kCatchState, kCatchComplete, kExceptionValue, kRejectExit,
  kCatchReturn, kAsyncCatchSize
};
enum class ProtocolPart : std::uint8_t { kSource, kResume, kYieldPrefix };
struct ResumeRegion {
  analysis::YieldRegion origin;
  std::size_t cleanup, size;
};
struct ResumeSlots { abc::RegisterId result, mode; };
ResumeSlots ResumePrefix(const abc::MethodBody& body, std::size_t begin,
    abc::RegisterId state, const abc::FrameLayout& frame) {
  Require(begin <= body.instructions.size() && kReturnMode <= body.instructions.size() - begin);
  const auto at = [&](ResumeStep step) -> const auto& { return body.instructions[begin + step]; };
  Require(Op(at(kSuspend), Opcode::kSUSPENDGENERATOR_V8) && Reg(at(kSuspend)) == state);
  for (auto step : {kStateForValue, kStateForMode})
    Require(Op(at(step), Opcode::kLDA_V8) && Reg(at(step)) == state);
  Require(Op(at(kResume), Opcode::kRESUMEGENERATOR) && Op(at(kMode), Opcode::kGETRESUMEMODE) &&
      Op(at(kStoreResult), Opcode::kSTA_V8) && Op(at(kStoreMode), Opcode::kSTA_V8));
  const auto result = Reg(at(kStoreResult)), mode = Reg(at(kStoreMode));
  Require(result != mode && result != state && mode != state && result.value < frame.locals && mode.value < frame.locals);
  return {result, mode};
}
// HandleCompletion emits its preserving tail even after Compile(finalizer)
// returned/threw. Reachability is confined to this owned RETURN corridor;
// external entries are rejected separately, never silently pruned. The common
// normal CFG owns transfer semantics. Disjoint slices cost O(I + B log I).
std::vector<std::uint8_t> ReachableCleanup(const abc::MethodBody& body,
    std::size_t begin, std::size_t count, std::uint64_t registers, std::size_t bytes) {
  enum class Accumulator : std::uint8_t { kUnseen, kDefined, kUndefined };
  constexpr std::size_t kVisits = 2; // Unseen -> Defined -> Undefined, never back.
  constexpr auto kScratch = sizeof(abc::BodyInstruction) + sizeof(std::uint8_t) +
      sizeof(Accumulator) + kVisits * sizeof(analysis::BlockId);
  if (count > bytes / kScratch) throw Error::kResourceLimit;
  bytes -= count * kScratch;
  abc::MethodBody slice;
  slice.instructions.assign(body.instructions.begin() + begin, body.instructions.begin() + begin + count);
  const auto base = slice.instructions.front().offset.value;
  for (auto& site : slice.instructions) {
    site.offset.value -= base;
    if (site.branch_target) site.branch_target->value -= base;
  }
  auto built = analysis::BuildNormalFlow(slice, {bytes});
  if (const auto* failure = std::get_if<analysis::FlowFailure>(&built))
    throw failure->kind == analysis::FlowError::kResourceLimit ? Error::kResourceLimit : Error::kKind;
  const auto& graph = std::get<analysis::NormalFlow>(built);
  std::vector<std::uint8_t> retained(count);
  std::vector<Accumulator> incoming(graph.blocks.size(), Accumulator::kUnseen);
  std::vector<analysis::BlockId> queue;
  queue.reserve(kVisits * graph.blocks.size()); queue.push_back({0}); incoming.front() = Accumulator::kUndefined;
  // Must-availability on normal Commit edges: a throwing call never reaches
  // its normal successor. Native RETURN has no dispatch acc at slice entry.
  // A later undefined predecessor invalidates an earlier defined input. Each
  // block/edge is processed at most twice; SSA still owns values/phi copies.
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const auto id = queue[head]; const auto& block = graph.blocks[id.value];
    bool accumulator = incoming[id.value] == Accumulator::kDefined;
    for (std::size_t i = block.first_instruction; i < block.first_instruction + block.instruction_count; ++i) {
      const auto access = abc::GetRegisterAccesses(slice.instructions[i].decoded, registers);
      const auto* known = std::get_if<abc::RegisterAccesses>(&access); Require(known != nullptr);
      Require(accumulator || !known->accumulator_read);
      accumulator |= known->accumulator_write;
    }
    std::fill(retained.begin() + block.first_instruction,
        retained.begin() + block.first_instruction + block.instruction_count, 1);
    for (std::size_t i = 0; i < block.successor_count; ++i) {
      const auto target = block.successors[i].target;
      auto& state = incoming[target.value];
      const auto out = accumulator ? Accumulator::kDefined : Accumulator::kUndefined;
      if (state == Accumulator::kUnseen || (state == Accumulator::kDefined && out == Accumulator::kUndefined)) {
        state = out; queue.push_back(target);
      }
    }
  }
  return retained;
}
ResumeRegion Resume(const abc::MethodBody& body, std::size_t begin,
    abc::RegisterId state, const abc::FrameLayout& frame) {
  Require(begin <= body.instructions.size() && kResumeSize <= body.instructions.size() - begin);
  const auto& jump = body.instructions[begin + kReturnBranch];
  Require(jump.branch_target.has_value());
  const auto target = std::lower_bound(body.instructions.begin() + begin, body.instructions.end(), *jump.branch_target,
      [](const auto& site, auto pc) { return site.offset < pc; });
  Require(target != body.instructions.end() && target->offset == *jump.branch_target);
  const auto next_mode = static_cast<std::size_t>(target - body.instructions.begin()) - begin;
  Require(next_mode >= kThrowMode);
  const auto cleanup = next_mode - kThrowMode;
  Require(cleanup <= body.instructions.size() - begin - kResumeSize);
  const auto at = [&](ResumeStep step) -> const auto& {
    return body.instructions[begin + step + (step >= kReturnValue ? cleanup : 0)];
  };
  const auto [result, mode] = ResumePrefix(body, begin, state, frame);
  for (auto step : {kReturnValue, kThrowValue, kContinuation})
    Require(Op(at(step), Opcode::kLDA_V8) && Reg(at(step)) == result);
  Require(Op(at(kReturn), Opcode::kRETURN) && Op(at(kThrow), Opcode::kTHROW_PREF_NONE));
  for (auto step : {kReturnEqual, kThrowEqual})
    Require(Op(at(step), Opcode::kEQ_IMM8_V8) && Reg(at(step), 1) == mode);
  const auto is_mode = [&](ResumeStep step, analysis::CoroutineResumeMode expected) {
    const auto& site = at(step);
    return Op(site, Opcode::kLDAI_IMM32) && site.decoded.operand_count == 1 &&
        site.decoded.operands[0].kind == abc::OperandKind::kImmediate &&
        site.decoded.operands[0].raw_bits == static_cast<std::uint32_t>(expected);
  };
  Require(is_mode(kReturnMode, analysis::CoroutineResumeMode::kReturn) &&
      is_mode(kThrowMode, analysis::CoroutineResumeMode::kThrow));
  for (auto step : {kReturnBranch, kThrowBranch}) {
    // Reuse the common decoded predicate across OH7 op_imm_8/16/32 widths.
    // A same-target JNEZ is not interchangeable: it reverses completion dispatch.
    const auto condition = analysis::LiftBranchCondition(at(step).decoded);
    const auto* predicate = std::get_if<analysis::UnaryExpression>(&condition);
    Require(predicate && predicate->op == analysis::UnaryOperator::kFalseOrZero);
  }
  Require(at(kReturnBranch).branch_target == at(kThrowMode).offset &&
      at(kThrowBranch).branch_target == at(kContinuation).offset);
  analysis::YieldRegion region{{}, at(kSuspend).offset, at(kResume).offset, at(kStoreResult).offset,
      at(kMode).offset, at(kStoreMode).offset, at(kContinuation).offset, state, {}, result, mode,
      at(kThrowBranch).offset, at(kThrow).offset};
  if (cleanup) region.return_route = analysis::YieldReturnRoute{at(kReturnBranch).offset,
      body.instructions[begin + kReturnValue].offset, at(kReturnValue).offset, at(kReturn).offset};
  return {region, cleanup, kResumeSize + cleanup};
}
std::vector<std::uint8_t> ValidateCleanup(const abc::MethodBody& body, std::size_t begin,
    ResumeRegion& resume, const abc::FrameLayout& frame,
    std::span<const ProtocolPart> protocol, std::size_t bytes) {
  const auto state = resume.origin.state, result = resume.origin.result, mode = resume.origin.resume_mode;
  std::vector<std::uint8_t> retained;
  if (resume.cleanup) {
    const auto cleanup = resume.cleanup;
    auto& region = resume.origin;
    if (cleanup > bytes / sizeof(abc::CodeOffset)) throw Error::kResourceLimit;
    std::vector<abc::CodeOffset> entries;
    entries.reserve(cleanup);
    if (entries.capacity() > bytes / sizeof(abc::CodeOffset)) throw Error::kResourceLimit;
    bool abrupt = false;
    for (std::size_t i = begin + kReturnValue; i < begin + kReturnValue + cleanup; ++i) {
      const auto& site = body.instructions[i];
      const auto control = abc::GetControlTransfer(site.decoded.opcode);
      Require(control && (*control == abc::ControlTransfer::kFallthrough ||
          *control == abc::ControlTransfer::kBranch || *control == abc::ControlTransfer::kJump ||
          *control == abc::ControlTransfer::kReturn || *control == abc::ControlTransfer::kThrow ||
          (*control == abc::ControlTransfer::kSuspend && protocol[i] == ProtocolPart::kResume)));
      abrupt |= *control == abc::ControlTransfer::kReturn || *control == abc::ControlTransfer::kThrow;
      if (*control == abc::ControlTransfer::kBranch || *control == abc::ControlTransfer::kJump) {
        Require(site.branch_target && *site.branch_target >= region.return_route->cleanup &&
            *site.branch_target <= region.return_route->payload);
        entries.push_back(*site.branch_target);
      }
    }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    bool accumulator = false, start = true, needs_flow = abrupt;
    std::size_t entry = 0;
    for (std::size_t i = begin + kReturnValue; i < begin + kReturnValue + cleanup; ++i) {
      const auto& site = body.instructions[i];
      const auto control = abc::GetControlTransfer(site.decoded.opcode);
      // Fast path for block-local acc. Cross-block reads need the owned CFG's
      // must-availability check, not rejection or a dispatch-derived input.
      const bool target = entry < entries.size() && entries[entry] == site.offset;
      if (start || target) accumulator = false;
      if (target) ++entry;
      const auto access = abc::GetRegisterAccesses(site.decoded, frame.registers);
      const auto* known = std::get_if<abc::RegisterAccesses>(&access); Require(known != nullptr);
      needs_flow |= !accumulator && known->accumulator_read;
      accumulator |= known->accumulator_write;
      start = *control != abc::ControlTransfer::kFallthrough;
      // Native RETURN bypasses the result/mode commits, and its payload cannot
      // be read from JS finally. No private resume slot may escape or be clobbered.
      for (std::size_t w = 0; w < known->count; ++w)
        for (auto slot : {state, result, mode}) {
          const auto& window = known->windows[w];
          Require((slot == state && protocol[i] == ProtocolPart::kResume) || slot.value < window.first.value ||
              slot.value - window.first.value >= window.count);
        }
    }
    if (needs_flow) {
      retained = ReachableCleanup(body, begin + kReturnValue, cleanup + kPreservingTailSize,
          frame.registers, bytes - entries.capacity() * sizeof(abc::CodeOffset));
      if (!retained.back()) region.return_route->completion.reset();
    }
  }
  return retained;
}
}  // namespace
std::variant<GeneratorBody, FunctionEmitError, abc::MetadataError> NormalizeGenerator(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    abc::ProfileId profile, ResourceFailureScope resources, std::size_t bytes, const GeneratorNames* names,
    GeneratorExecution execution) {
  try {
    Require(resources == ResourceFailureScope::kExcluded &&
        abc::HasPinnedRuntimeSemantics(profile));
    const auto& code = body.instructions;
    Require(execution == GeneratorExecution::kSynchronous || execution == GeneratorExecution::kAsynchronous);
    const bool asynchronous = execution == GeneratorExecution::kAsynchronous;
    constexpr std::size_t kEntryPrefix = 3, kYieldPrefix = 3;
    Require(!code.empty());
    std::size_t enter = 0;
    // OH7 PandaGen::{InitializeLexEnv,CopyFunctionArguments} prepend fresh
    // VM-only environment allocation and frame copies. Retain them in shared
    // IR. Delaying this unobservable prefix until first next is allowed only
    // with allocation failure excluded; no user effects/default evaluation.
    const auto lexical = analysis::LiftLexicalInstruction(code.front().decoded, frame.registers);
    const auto* environment = std::get_if<analysis::PushEnvironment>(&lexical);
    if (environment) ++enter;
    const auto copy_begin = enter;
    const auto is_frame_copy = [](const auto& site) {
      return Op(site, Opcode::kMOV_V4_V4) || Op(site, Opcode::kMOV_V8_V8) || Op(site, Opcode::kMOV_V16_V16);
    };
    const auto is_cell_copy = [&](std::size_t i) {
      if (!environment || i + 1 >= code.size() || !Op(code[i], Opcode::kLDA_V8) ||
          Reg(code[i]).value < frame.locals || Reg(code[i]).value >= frame.registers) return false;
      const auto lifted = analysis::LiftLexicalInstruction(code[i + 1].decoded, frame.registers);
      const auto* store = std::get_if<analysis::LexicalStore>(&lifted);
      // Only a fresh private cell may receive this immutable ABI value. An
      // outer store would change an observable capture at invocation time.
      return store && std::holds_alternative<analysis::Accumulator>(store->value) &&
          store->address.depth.value == 0 && store->address.slot.value < environment->slots;
    };
    while (enter < code.size()) {
      if (is_frame_copy(code[enter])) ++enter;
      else if (is_cell_copy(enter)) enter += 2;
      else break;
    }
    const auto first = enter + kEntryPrefix + (asynchronous ? kReturnMode : kResumeSize);
    Require(code.size() > first + 1 && frame.function &&
        Op(code[enter], asynchronous ? Opcode::kCREATEASYNCGENERATOROBJ_V8 : Opcode::kCREATEGENERATOROBJ_V8) &&
        Op(code[enter + 1], Opcode::kSTA_V8) && Op(code[enter + 2], Opcode::kLDUNDEFINED));
    const auto state = Reg(code[enter + 1]); Require(state.value < frame.locals);
    ResumeSlots initial{};
    if (asynchronous) {
      const auto slots = ResumePrefix(body, enter + kEntryPrefix, state, frame);
      initial = slots;
    } else {
      const auto entry_resume = Resume(body, enter + kEntryPrefix, state, frame);
      Require(!entry_resume.cleanup); initial = {entry_resume.origin.result, entry_resume.origin.resume_mode};
    }
    const auto callee = Reg(code[enter]);
    bool same_function = callee == *frame.function;
    for (std::size_t i = copy_begin; i < enter;) {
      const auto& copy = code[i];
      if (!is_frame_copy(copy)) { Require(is_cell_copy(i)); i += 2; continue; }
      Require(copy.decoded.operand_count == 2 && Reg(copy).value < frame.locals && Reg(copy) != state &&
          Reg(copy) != initial.result && Reg(copy) != initial.mode &&
          Reg(copy, 1).value >= frame.locals && Reg(copy, 1).value < frame.registers);
      // FunctionRegScope binds the callee to a copied local. All admitted
      // sources are immutable ABI inputs; the last write decides its identity.
      if (Reg(copy) == callee) same_function = Reg(copy, 1) == *frame.function;
      ++i;
    }
    Require(same_function);
    const auto handler = code.size() - (asynchronous ? kAsyncCatchSize : 1);
    const auto complete = [&](std::size_t load) {
      Require(Op(code[load], Opcode::kLDA_V8) && Reg(code[load]) == state);
      const auto& site = code[load + 1].decoded;
      Require(site.opcode == Opcode::kSETGENERATORSTATE_IMM8 && site.operand_count == 1 &&
          site.operands[0].kind == abc::OperandKind::kImmediate &&
          site.operands[0].raw_bits == static_cast<std::uint8_t>(analysis::GeneratorState::kCompleted));
    };
    if (asynchronous) {
      Require(handler >= first + kAsyncExitSize && Op(code[handler], Opcode::kSTA_V8));
      const auto caught = Reg(code[handler]); Require(caught != state && caught.value < frame.locals);
      complete(handler + kCatchState);
      Require(Op(code[handler + kExceptionValue], Opcode::kLDA_V8) && Reg(code[handler + kExceptionValue]) == caught &&
          Op(code[handler + kRejectExit], Opcode::kASYNCGENERATORREJECT_V8) && Reg(code[handler + kRejectExit]) == state &&
          Op(code[handler + kCatchReturn], Opcode::kRETURN));
      const auto exit = handler - kAsyncExitSize;
      Require(Op(code[exit + kImplicitValue], Opcode::kLDUNDEFINED) && Op(code[exit + kSaveExitValue], Opcode::kSTA_V8) &&
          Op(code[exit + kCanSuspend], Opcode::kLDTRUE) && Op(code[exit + kSaveCanSuspend], Opcode::kSTA_V8));
      const auto value = Reg(code[exit + kSaveExitValue]), flag = Reg(code[exit + kSaveCanSuspend]);
      Require(value != flag && value != state && flag != state && value.value < frame.locals && flag.value < frame.locals);
      complete(exit + kExitState);
      Require(Op(code[exit + kResolveExit], Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8) && Reg(code[exit + kResolveExit]) == state &&
          Reg(code[exit + kResolveExit], 1) == value && Reg(code[exit + kResolveExit], 2) == flag && Op(code[exit + kExitReturn], Opcode::kRETURN));
    } else Require(Op(code.back(), Opcode::kTHROW_PREF_NONE));
    // The completion catch may also cover the non-throwing prefix (fresh
    // environment, frame copies, fresh-cell copies; allocation failure is
    // excluded above): this SDK opens it at PC 0. Covering instructions that
    // cannot throw changes nothing, so accept a start anywhere in that prefix.
    auto catch_begin = code[enter + (asynchronous ? 2 : 0)].offset;
    if (!asynchronous && !body.tries.empty() && body.tries.back().offset < catch_begin) {
      const auto start = std::lower_bound(code.begin(), code.end(), body.tries.back().offset,
          [](const auto& site, auto pc) { return site.offset < pc; });
      if (start != code.end() && start->offset == body.tries.back().offset)
        catch_begin = body.tries.back().offset;
    }
    Require(HasAsyncCompletionCatch(body, catch_begin, code[handler].offset,
            std::uint64_t(code.back().offset.value) + code.back().decoded.size));
    // RETURN cleanup stays in the common graph; tree composition must route it
    // on native RETURN. Do not erase it or treat it as a normal resume effect.
    constexpr auto kPerInstruction = sizeof(abc::BodyInstruction) + sizeof(abc::CodeOffset) + 2 * sizeof(std::uint8_t) + sizeof(ProtocolPart);
    struct YieldProtocol { std::size_t prefix, suspend; ResumeRegion resume; };
    if (code.size() > bytes / kPerInstruction) return Error::kResourceLimit;
    const auto yield_count = static_cast<std::size_t>(std::count_if(code.begin(), code.end(),
        [](const auto& site) { return Op(site, Opcode::kCREATEITERRESULTOBJ_V8_V8) ||
            Op(site, Opcode::kGETITERATOR_IMM8) || Op(site, Opcode::kGETITERATOR_IMM16) ||
            Op(site, Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8); }));
    if (yield_count > (bytes - code.size() * kPerInstruction) / (sizeof(analysis::YieldSite) + sizeof(YieldProtocol) + sizeof(GeneratorDelegate)))
      return Error::kResourceLimit;
    GeneratorBody result;
    result.body.instructions.reserve(code.size()); result.source_pcs.reserve(code.size());
    result.yields.reserve(yield_count);
    std::vector<std::uint8_t> interior(code.size()), retained(code.size(), 1);
    std::vector<ProtocolPart> protocol(code.size(), ProtocolPart::kSource);
    std::vector<YieldProtocol> yields;
    yields.reserve(yield_count);
    std::vector<GeneratorDelegate> delegates; delegates.reserve(yield_count);
    const auto storage = [&](std::size_t count, std::size_t width) {
      if (count > bytes / width) throw Error::kResourceLimit;
      bytes -= count * width;
    };
    storage(result.body.instructions.capacity(), sizeof(abc::BodyInstruction));
    storage(result.source_pcs.capacity(), sizeof(abc::CodeOffset));
    storage(result.yields.capacity(), sizeof(analysis::YieldSite));
    storage(interior.capacity(), sizeof(std::uint8_t));
    storage(retained.capacity(), sizeof(std::uint8_t));
    storage(protocol.capacity(), sizeof(ProtocolPart));
    storage(yields.capacity(), sizeof(YieldProtocol));
    storage(delegates.capacity(), sizeof(GeneratorDelegate));
    std::vector<analysis::AwaitRegion> await_regions;
    if (asynchronous) {
      auto found = analysis::FindAwaitRegions(body, profile, frame.registers, bytes);
      if (const auto* error = std::get_if<analysis::AwaitRegionFailure>(&found))
        return error->kind == analysis::AwaitRegionError::kResourceLimit ? Error::kResourceLimit : Error::kKind;
      await_regions = std::get<std::vector<analysis::AwaitRegion>>(std::move(found));
      storage(await_regions.capacity(), sizeof(analysis::AwaitRegion));
      result.awaits.reserve(await_regions.size()); storage(result.awaits.capacity(), sizeof(analysis::AwaitSite));
      // Shared AwaitRegion owns pairing, rejection and exception boundaries.
      // Retain the same await/result/mode/continuation operations used by async
      // functions. Native rejection skips the VM commits; catch-input analysis
      // must prove those lost writes unobserved by each user catch.
      std::size_t ordinary = 0;
      for (std::size_t r = 0; r < await_regions.size(); ++r) {
        const auto region = await_regions[r];
        Require(region.state == state && region.result.value < frame.locals && region.resume_mode.value < frame.locals &&
            region.await >= code[first].offset && region.continuation < code[handler - kAsyncExitSize].offset);
        const auto begin = std::lower_bound(code.begin() + first, code.begin() + handler, region.await,
            [](const auto& site, auto pc) { return site.offset < pc; });
        Require(begin != code.begin() + handler && begin->offset == region.await && region.instructions);
        const auto index = static_cast<std::size_t>(begin - code.begin());
        Require(analysis::kAwaitProtocolInstructions <= handler - index);
        // An intrinsic outbound wait is followed by AsyncYield settlement.
        // Its injected RETURN wait belongs to the same native yield. Keep
        // explicit source awaits in the common AwaitSite list independently.
        constexpr auto kAsyncYieldResolve = analysis::kAwaitProtocolInstructions + 5;
        if (kAsyncYieldResolve < handler - index && Op(code[index + kAsyncYieldResolve], Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8)) {
          Require(r + 1 < await_regions.size() && kAsyncYieldReturnAwait < handler - index &&
              await_regions[r + 1].await == code[index + kAsyncYieldReturnAwait].offset);
          auto parsed = ParseAsyncGeneratorYield(body, index, state, frame, region, await_regions[r + 1]);
          if (const auto* error = std::get_if<Error>(&parsed)) return *error;
          const auto& yielded = std::get<AsyncGeneratorYield>(parsed);
          Require(yielded.end <= handler - kAsyncExitSize);
          const auto& origin = yielded.origin;
          for (std::size_t i = yielded.begin; i < yielded.end; ++i) {
            const auto pc = code[i].offset;
            interior[i] = i != yielded.begin;
            retained[i] = pc == origin.create || pc == origin.result_store || pc == origin.mode ||
                pc == origin.mode_store || pc == origin.continuation;
            protocol[i] = ProtocolPart::kResume;
          }
          result.yields.push_back({origin.create, origin.mode, origin});
          ++r; continue;
        }
        for (std::size_t step = 0; step < analysis::kAwaitProtocolInstructions; ++step) {
          const auto pc = code[index + step].offset;
          Require((*region.instructions)[step] == pc);
          // Continuation is retained but also private to this await's normal
          // entry. Source branches/handler entries cannot bypass its commits.
          interior[index + step] = step != 0;
          retained[index + step] = pc == region.await || pc == region.result_store || pc == region.mode ||
              pc == region.mode_store || pc == region.continuation;
          protocol[index + step] = ProtocolPart::kResume;
        }
        result.awaits.push_back({region.await, region, region.mode});
        await_regions[ordinary++] = region;
      }
      await_regions.resize(ordinary);
      const auto catch_inputs = analysis::ProveAwaitCatchInputs(body, await_regions, profile, frame.registers,
          code[handler].offset, {bytes}, code[handler - kAsyncExitSize].offset);
      if (const auto* error = std::get_if<analysis::AwaitCatchFailure>(&catch_inputs))
        return error->kind == analysis::AwaitCatchError::kResourceLimit ? Error::kResourceLimit : Error::kKind;
    }
    std::fill(interior.begin(), interior.begin() + first, 1);
    if (asynchronous) {
      // Exact Prepare and implicit return/catch settlement, with shared awaits
      // above. Source yield/explicit returns remain unsupported. Erase only VM
      // operations, keeping the actual implicit undefined load and Return.
      const auto exit = handler - kAsyncExitSize;
      std::fill(retained.begin() + exit + kSaveExitValue, retained.begin() + exit + kExitReturn, 0);
      std::fill(interior.begin() + exit + kSaveExitValue, interior.begin() + handler, 1);
    }
    const auto retain = [&](std::size_t i) {
      result.body.instructions.push_back(code[i]); result.source_pcs.push_back(code[i].offset);
    };
    // Parse exact handshakes first, including yields compiled inside RETURN
    // cleanup. handleReturn_ suppresses recursive outer cleanup injection in
    // pinned OH7 FunctionBuilder::HandleCompletion; nested cleanup routes stay
    // unsupported. Thus each instruction belongs to at most one cleanup and
    // one handshake: O(I + Y log I), no repeated nested-slice graph walks.
    if (names && !asynchronous) for (std::size_t i = first; i < handler; ++i) {
      if (!Op(code[i], Opcode::kGETITERATOR_IMM8) && !Op(code[i], Opcode::kGETITERATOR_IMM16)) continue;
      auto parsed = ParseGeneratorDelegate(body, i, state, frame, *names, bytes);
      if (const auto* error = std::get_if<abc::MetadataError>(&parsed)) return *error;
      if (const auto* error = std::get_if<Error>(&parsed)) {
        if (*error == Error::kResourceLimit) return *error;
        continue;
      }
      auto delegate = std::get<GeneratorDelegate>(std::move(parsed));
      Require(delegate.end < handler);
      std::fill(interior.begin() + i + 1, interior.begin() + delegate.end, 1);
      std::fill(retained.begin() + i + 1, retained.begin() + delegate.end, 0);
      std::fill(protocol.begin() + i, protocol.begin() + delegate.end, ProtocolPart::kResume);
      delegates.push_back(std::move(delegate)); i = delegates.back().end - 1;
    }
    std::size_t cleanup_end = 0;
    if (!asynchronous) for (std::size_t i = first; i + kYieldPrefix < handler; ++i) {
      if (protocol[i] == ProtocolPart::kResume || !Op(code[i + kYieldPrefix], Opcode::kCREATEITERRESULTOBJ_V8_V8)) continue;
      Require(Op(code[i], Opcode::kSTA_V8) && Op(code[i + 1], Opcode::kLDFALSE) && Op(code[i + 2], Opcode::kSTA_V8));
      const auto value = Reg(code[i]), done = Reg(code[i + 2]);
      Require(value != done && value != state && done != state && value.value < frame.locals && done.value < frame.locals &&
          Reg(code[i + kYieldPrefix]) == value && Reg(code[i + kYieldPrefix], 1) == done);
      const auto suspend = i + kYieldPrefix + 1;
      Require(kResumeSize <= handler - suspend);
      auto resume = Resume(body, suspend, state, frame);
      Require(resume.size <= handler - suspend);
      Require(i >= cleanup_end || (!resume.cleanup && suspend + resume.size <= cleanup_end));
      if (resume.cleanup) cleanup_end = suspend + kReturnValue + resume.cleanup;
      auto& region = resume.origin;
      region.create = code[i + kYieldPrefix].offset; region.value = value;
      // Only validated VM protocol operations may access the state register.
      // An inner result/mode may never alias an outer pending completion.
      std::fill(protocol.begin() + i + 1, protocol.begin() + suspend, ProtocolPart::kYieldPrefix);
      std::fill(protocol.begin() + suspend, protocol.begin() + suspend + kReturnValue, ProtocolPart::kResume);
      std::fill(protocol.begin() + suspend + kReturnValue + resume.cleanup,
          protocol.begin() + suspend + resume.size, ProtocolPart::kResume);
      yields.push_back({i, suspend, std::move(resume)});
    }
    std::fill(retained.begin() + enter, retained.begin() + first, 0);
    retained[handler] = 0;
    for (auto& parsed : yields) {
      const auto i = parsed.prefix, suspend = parsed.suspend;
      auto& resume = parsed.resume;
      if (resume.cleanup) {
        const auto nested = std::lower_bound(delegates.begin(), delegates.end(), parsed.prefix,
            [](const auto& delegate, auto index) { return delegate.begin < index; });
        Require(nested == delegates.end() || nested->begin >= suspend + kReturnValue + resume.cleanup);
      }
      const auto cleanup = ValidateCleanup(body, suspend, resume, frame, protocol, bytes);
      const bool reachable = retained[i];
      std::fill(interior.begin() + i + 1, interior.begin() + suspend + resume.size, 1);
      // Strip protocol, leaving ordinary cleanup instructions in the shared IR.
      std::fill(retained.begin() + suspend, retained.begin() + suspend + resume.size, 0);
      if (!reachable) { std::fill(retained.begin() + i, retained.begin() + suspend, 0); continue; }
      for (auto step : {kStoreResult, kMode, kStoreMode}) retained[suspend + step] = 1;
      if (resume.cleanup) {
        for (std::size_t step = kReturnMode; step < kThrowMode + resume.cleanup; ++step) {
          if (step >= kReturnValue && !cleanup.empty() && !cleanup[step - kReturnValue]) continue;
          retained[suspend + step] = 1;
        }
      }
      retained[suspend + kContinuation + resume.cleanup] = 1;
      const auto& region = resume.origin;
      result.yields.push_back({region.create, region.mode, region});
    }
    for (const auto& delegate : delegates) {
      Require(retained[delegate.begin]);
      result.yields.push_back({delegate.origin.create, {}, delegate.origin});
    }
    std::sort(result.yields.begin(), result.yields.end(), [](const auto& a, const auto& b) { return a.origin.create < b.origin.create; });
    if (asynchronous) {
      // A user boundary cannot split intrinsic waits, NEXT commits or return
      // settlement, even at a retained VM store. Completion wrapper is owned
      // separately and intentionally starts inside Prepare.
      const auto boundary = [&](std::uint64_t pc) {
        const auto found = std::lower_bound(code.begin(), code.end(), pc,
            [](const auto& site, auto value) { return site.offset.value < value; });
        Require(found != code.end() && found->offset.value == pc && !interior[found - code.begin()]);
      };
      for (std::size_t t = 0; t + 1 < body.tries.size(); ++t) {
        boundary(body.tries[t].offset.value);
        boundary(std::uint64_t(body.tries[t].offset.value) + body.tries[t].length);
      }
      for (std::size_t h = 0; h + 1 < body.handlers.size(); ++h) {
        boundary(body.handlers[h].offset.value);
        boundary(std::uint64_t(body.handlers[h].offset.value) + body.handlers[h].length);
      }
    }
    std::size_t next_protocol = 0;
    for (std::size_t i = 0; i < handler; ++i) {
      if (!retained[i]) continue;
      const bool yield = next_protocol < result.yields.size() && code[i].offset == result.yields[next_protocol].origin.create;
      if (yield) ++next_protocol;
      if (protocol[i] != ProtocolPart::kResume) {
        const auto access = abc::GetRegisterAccesses(code[i].decoded, frame.registers);
        const auto* known = std::get_if<abc::RegisterAccesses>(&access); Require(known != nullptr);
        for (std::size_t j = 0; j < known->count; ++j) {
          const auto& window = known->windows[j];
          Require(state.value < window.first.value || state.value - window.first.value >= window.count);
          if (asynchronous && i >= first && i < handler - kAsyncExitSize) {
            // Initial resumed values/mode have no source counterpart. Reuse
            // of these locals needs a proved overwrite before observation;
            // that later capability is not silently admitted in this slice.
            for (const auto slot : {initial.result, initial.mode})
              Require(slot.value < window.first.value || slot.value - window.first.value >= window.count);
          }
        }
        const auto control = abc::GetControlTransfer(code[i].decoded.opcode);
        Require(control && *control != abc::ControlTransfer::kSuspend);
        if (asynchronous && *control == abc::ControlTransfer::kReturn) Require(i == handler - 1);
        Require(!Op(code[i], Opcode::kCREATEGENERATOROBJ_V8) && !Op(code[i], Opcode::kRESUMEGENERATOR) &&
            !Op(code[i], Opcode::kGETRESUMEMODE));
        // Only the exact certified create instruction survives as a native yield.
        if (Op(code[i], Opcode::kCREATEITERRESULTOBJ_V8_V8))
          Require(yield);
      }
      retain(i);
    }
    // Rewritten RETURN dispatch bypasses the erased VM THROW/NEXT handshake.
    std::size_t next_return = 0;
    for (auto& site : result.body.instructions) {
      while (next_return < result.yields.size() && (!result.yields[next_return].origin.return_route ||
          result.yields[next_return].origin.return_route->branch < site.offset)) ++next_return;
      if (next_return < result.yields.size() && result.yields[next_return].origin.return_route &&
          result.yields[next_return].origin.return_route->branch == site.offset)
        site.branch_target = result.yields[next_return].origin.continuation;
    }
    Require(!result.body.instructions.empty());
    // Selected catch must be identical at the native yield and VM THROW site;
    // result/mode writes lost on native THROW must be dead at handler entry.
    const auto catch_inputs = analysis::ProveYieldCatchInputs(
        body, result.yields, profile, frame.registers, code[handler].offset, {bytes},
        asynchronous ? std::optional{code[handler - kAsyncExitSize].offset} : std::nullopt, await_regions);
    if (const auto* error = std::get_if<analysis::AwaitCatchFailure>(&catch_inputs))
      return error->kind == analysis::AwaitCatchError::kResourceLimit ? Error::kResourceLimit : Error::kKind;
    abc::MethodBody handler_body;
    const abc::MethodBody* handler_input = &body;
    if (!delegates.empty()) {
      // Native yield* owns only the exact Iterator.Close catch. Every other
      // user handler remains; its boundaries cannot bisect a folded protocol.
      std::vector<std::uint8_t> internal(body.handlers.size());
      storage(internal.capacity(), sizeof(std::uint8_t));
      handler_body.tries.reserve(body.tries.size()); handler_body.handlers.reserve(body.handlers.size());
      storage(handler_body.tries.capacity(), sizeof(abc::TryRegion));
      storage(handler_body.handlers.capacity(), sizeof(abc::CatchHandler));
      for (const auto& region : body.tries) {
        const auto found = std::lower_bound(delegates.begin(), delegates.end(), region.offset,
            [](const auto& delegate, auto pc) { return delegate.close_begin < pc; });
        if (found != delegates.end() && found->close_begin == region.offset) {
          Require(region.length == found->close_end.value - found->close_begin.value && region.handler_count == 1 &&
              region.first_handler < body.handlers.size());
          const auto& caught = body.handlers[region.first_handler];
          Require(!caught.type && caught.offset == found->close_catch &&
              caught.length == found->close_catch_end.value - found->close_catch.value);
          internal[region.first_handler] = 1;
        }
      }
      const auto marked = std::count(internal.begin(), internal.end(), 1);
      Require(static_cast<std::size_t>(marked) == delegates.size());
      std::vector<std::uint32_t> removed(body.handlers.size() + 1);
      storage(removed.capacity(), sizeof(std::uint32_t));
      for (std::size_t i = 0; i < body.handlers.size(); ++i) {
        removed[i + 1] = removed[i] + internal[i];
        if (!internal[i]) handler_body.handlers.push_back(body.handlers[i]);
      }
      for (const auto& original : body.tries) {
        if (original.handler_count == 1 && internal[original.first_handler]) continue;
        Require(removed[original.first_handler] == removed[original.first_handler + original.handler_count]);
        auto region = original; region.first_handler -= removed[region.first_handler];
        handler_body.tries.push_back(region);
      }
      handler_body.instructions = body.instructions;
      storage(handler_body.instructions.capacity(), sizeof(abc::BodyInstruction));
      handler_input = &handler_body;
    }
    for (std::size_t i = 0; i + 1 < handler_input->handlers.size(); ++i) {
      const auto target = handler_input->handlers[i].offset;
      const auto site = std::lower_bound(code.begin(), code.end(), target,
          [](const auto& instruction, auto offset) { return instruction.offset < offset; });
      Require(site != code.end() && site->offset == target && !interior[site - code.begin()]);
    }
    std::uint32_t pc = 0;
    std::size_t next_yield = 0, next_mode = 0, next_await = 0, next_await_mode = 0;
    for (auto& site : result.body.instructions) {
      if (next_await < result.awaits.size() && site.offset == result.awaits[next_await].origin.await)
        result.awaits[next_await++].pc = {pc};
      if (next_await_mode < result.awaits.size() && site.offset == result.awaits[next_await_mode].origin.mode)
        result.awaits[next_await_mode++].mode_pc = {pc};
      if (next_yield < result.yields.size() && site.offset == result.yields[next_yield].origin.create)
        result.yields[next_yield++].pc = {pc};
      while (next_mode < result.yields.size() && !result.yields[next_mode].mode_pc) ++next_mode;
      if (next_mode < result.yields.size() && site.offset == result.yields[next_mode].origin.mode)
        result.yields[next_mode++].mode_pc = {pc};
      site.offset = {pc}; pc += site.decoded.size;
    }
    std::size_t next_owner = 0;
    const analysis::YieldRegion* owner = nullptr;
    for (std::size_t s = 0; s < result.body.instructions.size(); ++s) {
      auto& site = result.body.instructions[s];
      if (!site.branch_target) continue;
      const auto source = result.source_pcs[s];
      // Cleanup routes are disjoint. Inner yields have no route and cannot
      // steal the outer corridor's branch/tail ownership. One monotone cursor
      // visits each yield once, including arbitrarily many leaf suspensions.
      while (next_owner < result.yields.size() && result.yields[next_owner].origin.create <= source) {
        if (result.yields[next_owner].origin.return_route) owner = &result.yields[next_owner].origin;
        ++next_owner;
      }
      const auto* route = owner ? &*owner->return_route : nullptr;
      const bool owned_return = route && route->branch == source && site.branch_target == owner->continuation;
      const bool owned_cleanup = route && source >= route->cleanup && source < route->payload &&
          *site.branch_target >= route->cleanup && *site.branch_target <= route->payload;
      const auto target = *site.branch_target;
      const auto original = std::lower_bound(code.begin(), code.end(), target,
          [](const auto& instruction, auto offset) { return instruction.offset < offset; });
      Require(original != code.end() && original->offset == target &&
          (owned_return || (owned_cleanup && (protocol[original - code.begin()] == ProtocolPart::kSource || target == route->payload)) ||
           !interior[original - code.begin()]));
      const auto found = std::lower_bound(result.source_pcs.begin(), result.source_pcs.end(), target);
      Require(found != result.source_pcs.end() && *found == target);
      const auto destination = result.body.instructions[found - result.source_pcs.begin()].offset;
      const auto delta = std::int64_t(destination.value) - site.offset.value;
      Require(site.decoded.operand_count == 1);
      auto& operand = site.decoded.operands[0];
      Require(operand.kind == abc::OperandKind::kImmediate && operand.width_bits > 0 && operand.width_bits <= 32);
      const auto magnitude = std::int64_t{1} << (operand.width_bits - 1);
      Require(delta >= -magnitude && delta < magnitude);
      operand.raw_bits = static_cast<std::uint64_t>(delta) & ((std::uint64_t{1} << operand.width_bits) - 1);
      site.decoded.branch_delta = delta; site.branch_target = destination;
    }
    if (const auto error = RebaseAsyncHandlers(*handler_input, result.source_pcs, result.body, bytes)) return *error;
    return result;
  } catch (abc::MetadataError error) { return error;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
