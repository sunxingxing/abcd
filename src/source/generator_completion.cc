#include "generator_completion.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/await_catch.h"
#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/lexical.h"
#include "async_handlers.h"

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
analysis::YieldRegion Resume(const abc::MethodBody& body, std::size_t begin,
    abc::RegisterId state, const abc::FrameLayout& frame) {
  Require(begin <= body.instructions.size() && kResumeSize <= body.instructions.size() - begin);
  const auto at = [&](ResumeStep step) -> const auto& { return body.instructions[begin + step]; };
  Require(Op(at(kSuspend), Opcode::kSUSPENDGENERATOR_V8) && Reg(at(kSuspend)) == state);
  for (auto step : {kStateForValue, kStateForMode})
    Require(Op(at(step), Opcode::kLDA_V8) && Reg(at(step)) == state);
  Require(Op(at(kResume), Opcode::kRESUMEGENERATOR) && Op(at(kMode), Opcode::kGETRESUMEMODE) &&
      Op(at(kStoreResult), Opcode::kSTA_V8) && Op(at(kStoreMode), Opcode::kSTA_V8));
  const auto result = Reg(at(kStoreResult)), mode = Reg(at(kStoreMode));
  Require(result != mode && result != state && mode != state && result.value < frame.locals && mode.value < frame.locals);
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
  return {{}, at(kSuspend).offset, at(kResume).offset, at(kStoreResult).offset,
      at(kMode).offset, at(kStoreMode).offset, at(kContinuation).offset, state, {}, result, mode,
      at(kThrowBranch).offset, at(kThrow).offset};
}
}  // namespace
std::variant<GeneratorBody, FunctionEmitError> NormalizeGenerator(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    abc::ProfileId profile, ResourceFailureScope resources, std::size_t bytes) {
  try {
    Require(resources == ResourceFailureScope::kExcluded &&
        (profile == abc::ProfileId::kDynamic1202 || profile == abc::ProfileId::kDynamic1206 ||
         profile == abc::ProfileId::kDynamic1301));
    const auto& code = body.instructions;
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
    const auto first = enter + kEntryPrefix + kResumeSize;
    Require(code.size() > first + 1 && frame.function &&
        Op(code[enter], Opcode::kCREATEGENERATOROBJ_V8) &&
        Op(code[enter + 1], Opcode::kSTA_V8) && Op(code[enter + 2], Opcode::kLDUNDEFINED));
    const auto state = Reg(code[enter + 1]); Require(state.value < frame.locals);
    const auto initial = Resume(body, enter + kEntryPrefix, state, frame);
    const auto callee = Reg(code[enter]);
    bool same_function = callee == *frame.function;
    for (std::size_t i = copy_begin; i < enter;) {
      const auto& copy = code[i];
      if (!is_frame_copy(copy)) { Require(is_cell_copy(i)); i += 2; continue; }
      Require(copy.decoded.operand_count == 2 && Reg(copy).value < frame.locals && Reg(copy) != state &&
          Reg(copy) != initial.result && Reg(copy) != initial.resume_mode &&
          Reg(copy, 1).value >= frame.locals && Reg(copy, 1).value < frame.registers);
      // FunctionRegScope binds the callee to a copied local. All admitted
      // sources are immutable ABI inputs; the last write decides its identity.
      if (Reg(copy) == callee) same_function = Reg(copy, 1) == *frame.function;
      ++i;
    }
    Require(same_function);
    const auto handler = code.size() - 1;
    Require(Op(code.back(), Opcode::kTHROW_PREF_NONE) &&
        HasAsyncCompletionCatch(body, code[enter].offset, code.back().offset,
            std::uint64_t(code.back().offset.value) + code.back().decoded.size));
    // Only direct-return resume protocols are collapsed here. In particular,
    // an injected finalizer between ReturnBranch and ReturnValue fails Resume.
    constexpr auto kPerInstruction = sizeof(abc::BodyInstruction) + sizeof(abc::CodeOffset) + sizeof(std::uint8_t);
    if (code.size() > bytes / kPerInstruction) return Error::kResourceLimit;
    const auto yield_count = static_cast<std::size_t>(std::count_if(code.begin(), code.end(),
        [](const auto& site) { return Op(site, Opcode::kCREATEITERRESULTOBJ_V8_V8); }));
    if (yield_count > (bytes - code.size() * kPerInstruction) / sizeof(analysis::YieldSite))
      return Error::kResourceLimit;
    GeneratorBody result;
    result.body.instructions.reserve(code.size()); result.source_pcs.reserve(code.size());
    result.yields.reserve(yield_count);
    std::vector<std::uint8_t> interior(code.size());
    const auto storage = [&](std::size_t count, std::size_t width) {
      if (count > bytes / width) throw Error::kResourceLimit;
      bytes -= count * width;
    };
    storage(result.body.instructions.capacity(), sizeof(abc::BodyInstruction));
    storage(result.source_pcs.capacity(), sizeof(abc::CodeOffset));
    storage(result.yields.capacity(), sizeof(analysis::YieldSite));
    storage(interior.capacity(), sizeof(std::uint8_t));
    std::fill(interior.begin(), interior.begin() + first, 1);
    const auto retain = [&](std::size_t i) {
      result.body.instructions.push_back(code[i]); result.source_pcs.push_back(code[i].offset);
    };
    for (std::size_t i = 0; i < enter; ++i) retain(i);
    for (std::size_t i = first; i < handler;) {
      if (i + kYieldPrefix < handler && Op(code[i + kYieldPrefix], Opcode::kCREATEITERRESULTOBJ_V8_V8)) {
        Require(Op(code[i], Opcode::kSTA_V8) && Op(code[i + 1], Opcode::kLDFALSE) && Op(code[i + 2], Opcode::kSTA_V8));
        const auto value = Reg(code[i]), done = Reg(code[i + 2]);
        Require(value != done && value != state && done != state && value.value < frame.locals && done.value < frame.locals &&
            Reg(code[i + kYieldPrefix]) == value && Reg(code[i + kYieldPrefix], 1) == done);
        const auto suspend = i + kYieldPrefix + 1;
        Require(kResumeSize <= handler - suspend);
        auto region = Resume(body, suspend, state, frame);
        region.create = code[i + kYieldPrefix].offset; region.value = value;
        result.yields.push_back({region.create, region.mode, region});
        std::fill(interior.begin() + i + 1, interior.begin() + suspend + kResumeSize, 1);
        for (std::size_t prefix = 0; prefix <= kYieldPrefix; ++prefix) retain(i + prefix);
        for (auto step : {kStoreResult, kMode, kStoreMode, kContinuation}) retain(suspend + step);
        i = suspend + kResumeSize;
      } else {
        const auto access = abc::GetRegisterAccesses(code[i].decoded, frame.registers);
        const auto* known = std::get_if<abc::RegisterAccesses>(&access); Require(known != nullptr);
        for (std::size_t j = 0; j < known->count; ++j) {
          const auto& window = known->windows[j];
          Require(state.value < window.first.value || state.value - window.first.value >= window.count);
        }
        const auto control = abc::GetControlTransfer(code[i].decoded.opcode);
        Require(control && *control != abc::ControlTransfer::kSuspend);
        Require(!Op(code[i], Opcode::kCREATEGENERATOROBJ_V8) && !Op(code[i], Opcode::kRESUMEGENERATOR) &&
            !Op(code[i], Opcode::kGETRESUMEMODE) && !Op(code[i], Opcode::kCREATEITERRESULTOBJ_V8_V8));
        retain(i++);
      }
    }
    Require(!result.body.instructions.empty());
    // Selected catch must be identical at the native yield and VM THROW site;
    // result/mode writes lost on native THROW must be dead at handler entry.
    const auto catch_inputs = analysis::ProveYieldCatchInputs(
        body, result.yields, profile, frame.registers, code.back().offset, {bytes});
    if (const auto* error = std::get_if<analysis::AwaitCatchFailure>(&catch_inputs))
      return error->kind == analysis::AwaitCatchError::kResourceLimit ? Error::kResourceLimit : Error::kKind;
    for (std::size_t i = 0; i + 1 < body.handlers.size(); ++i) {
      const auto target = body.handlers[i].offset;
      const auto site = std::lower_bound(code.begin(), code.end(), target,
          [](const auto& instruction, auto offset) { return instruction.offset < offset; });
      Require(site != code.end() && site->offset == target && !interior[site - code.begin()]);
    }
    std::uint32_t pc = 0;
    std::size_t next_yield = 0, next_mode = 0;
    for (auto& site : result.body.instructions) {
      if (next_yield < result.yields.size() && site.offset == result.yields[next_yield].origin.create)
        result.yields[next_yield++].pc = {pc};
      if (next_mode < result.yields.size() && site.offset == result.yields[next_mode].origin.mode)
        result.yields[next_mode++].mode_pc = {pc};
      site.offset = {pc}; pc += site.decoded.size;
    }
    for (auto& site : result.body.instructions) {
      if (!site.branch_target) continue;
      const auto target = *site.branch_target;
      const auto original = std::lower_bound(code.begin(), code.end(), target,
          [](const auto& instruction, auto offset) { return instruction.offset < offset; });
      Require(original != code.end() && original->offset == target && !interior[original - code.begin()]);
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
    if (const auto error = RebaseAsyncHandlers(body, result.source_pcs, result.body, bytes)) return *error;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
