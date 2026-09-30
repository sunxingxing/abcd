#include "generator_async_yield.h"

#include <limits>
#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/coroutine.h"

namespace abcd::source {
namespace {
using abc::Opcode;
using Error = FunctionEmitError;
void Require(bool condition) { if (!condition) throw Error::kKind; }
// Pinned OH7 AsyncGeneratorFunctionBuilder::Yield and FunctionBuilder::AsyncYield.
// Five temporaries are allocated/reused inside nested RegScopes. No source
// effect, finalizer, handler boundary or external entry may be erased here.
enum Step : std::size_t {
  kWait = 0, kSaveValue = analysis::kAwaitProtocolInstructions,
  kState, kSuspendedYield, kFalse, kSaveDone, kResolve,
  kResumeState, kResume, kSaveResult, kModeState, kMode, kSaveMode,
  kReturnMode, kReturnEqual, kReturnBranch, kReturnValue, kReturnAwait,
  kSaveExit = kReturnAwait + analysis::kAwaitProtocolInstructions,
  kTrue, kSaveFlag, kExitState, kCompleted, kResolveReturn, kReturn,
  kThrowMode, kThrowEqual, kThrowBranch, kThrowValue, kThrow, kContinuation, kSize
};
static_assert(kReturnAwait == kAsyncYieldReturnAwait);
enum Slot : std::uint32_t { kValue, kResumeMode, kResult, kDone, kAwaitResult, kSlots };
}
std::variant<AsyncGeneratorYield, FunctionEmitError> ParseAsyncGeneratorYield(
    const abc::MethodBody& body, std::size_t begin, abc::RegisterId state,
    const abc::FrameLayout& frame, const analysis::AwaitRegion& value,
    const analysis::AwaitRegion& returned) {
  try {
    const auto& code = body.instructions;
    Require(begin <= code.size() && kSize <= code.size() - begin);
    const auto at = [&](std::size_t step) -> const auto& { return code[begin + step]; };
    const auto op = [&](std::size_t step, Opcode opcode) { Require(at(step).decoded.opcode == opcode); };
    const auto reg = [&](std::size_t step, std::size_t operand = 0) {
      const auto& site = at(step).decoded;
      Require(operand < site.operand_count && site.operands[operand].kind == abc::OperandKind::kRegister &&
          site.operands[operand].raw_bits <= std::numeric_limits<std::uint32_t>::max());
      return abc::RegisterId{static_cast<std::uint32_t>(site.operands[operand].raw_bits)};
    };
    op(kSaveValue, Opcode::kSTA_V8);
    const auto first = reg(kSaveValue);
    Require(first.value < frame.locals && kSlots <= frame.locals - first.value &&
        (state.value < first.value || state.value - first.value >= kSlots));
    const auto slot = [&](Slot offset) { return abc::RegisterId{first.value + offset}; };
    const auto immediate = [&](std::size_t step, Opcode opcode, std::uint32_t expected) {
      op(step, opcode); const auto& decoded = at(step).decoded;
      Require(decoded.operand_count == 1 && decoded.operands[0].kind == abc::OperandKind::kImmediate &&
          decoded.operands[0].raw_bits == expected);
    };
    const auto wait = [&](const analysis::AwaitRegion& region, std::size_t offset,
        abc::RegisterId result, abc::RegisterId mode) {
      Require(region.instructions && region.state == state && region.result == result && region.resume_mode == mode);
      for (std::size_t step = 0; step < analysis::kAwaitProtocolInstructions; ++step)
        Require((*region.instructions)[step] == at(offset + step).offset);
    };
    wait(value, kWait, slot(kAwaitResult), slot(kDone));
    wait(returned, kReturnAwait, slot(kResult), slot(kResumeMode));
    for (auto step : {kState, kResumeState, kModeState, kExitState}) {
      op(step, Opcode::kLDA_V8); Require(reg(step) == state);
    }
    immediate(kSuspendedYield, Opcode::kSETGENERATORSTATE_IMM8, static_cast<std::uint32_t>(analysis::GeneratorState::kSuspendedYield));
    immediate(kCompleted, Opcode::kSETGENERATORSTATE_IMM8, static_cast<std::uint32_t>(analysis::GeneratorState::kCompleted));
    op(kFalse, Opcode::kLDFALSE); op(kSaveDone, Opcode::kSTA_V8); Require(reg(kSaveDone) == slot(kDone));
    op(kResolve, Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8);
    Require(reg(kResolve) == state && reg(kResolve, 1) == slot(kValue) && reg(kResolve, 2) == slot(kDone));
    op(kResume, Opcode::kRESUMEGENERATOR); op(kSaveResult, Opcode::kSTA_V8); Require(reg(kSaveResult) == slot(kResult));
    op(kMode, Opcode::kGETRESUMEMODE); op(kSaveMode, Opcode::kSTA_V8); Require(reg(kSaveMode) == slot(kResumeMode));
    immediate(kReturnMode, Opcode::kLDAI_IMM32, static_cast<std::uint32_t>(analysis::CoroutineResumeMode::kReturn));
    immediate(kThrowMode, Opcode::kLDAI_IMM32, static_cast<std::uint32_t>(analysis::CoroutineResumeMode::kThrow));
    for (auto step : {kReturnEqual, kThrowEqual}) {
      op(step, Opcode::kEQ_IMM8_V8); Require(reg(step, 1) == slot(kResumeMode));
    }
    for (auto step : {kReturnBranch, kThrowBranch}) {
      const auto lifted = analysis::LiftBranchCondition(at(step).decoded);
      const auto* predicate = std::get_if<analysis::UnaryExpression>(&lifted);
      Require(predicate && predicate->op == analysis::UnaryOperator::kFalseOrZero);
    }
    Require(at(kReturnBranch).branch_target == at(kThrowMode).offset && at(kThrowBranch).branch_target == at(kContinuation).offset);
    for (auto step : {kReturnValue, kThrowValue, kContinuation}) {
      op(step, Opcode::kLDA_V8); Require(reg(step) == slot(kResult));
    }
    op(kSaveExit, Opcode::kSTA_V8); Require(reg(kSaveExit) == slot(kDone));
    op(kTrue, Opcode::kLDTRUE); op(kSaveFlag, Opcode::kSTA_V8); Require(reg(kSaveFlag) == slot(kAwaitResult));
    op(kResolveReturn, Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8);
    Require(reg(kResolveReturn) == state && reg(kResolveReturn, 1) == slot(kDone) && reg(kResolveReturn, 2) == slot(kAwaitResult));
    op(kReturn, Opcode::kRETURN); op(kThrow, Opcode::kTHROW_PREF_NONE);
    analysis::YieldRegion origin{value.await, value.suspend, at(kResume).offset, at(kSaveResult).offset,
        at(kMode).offset, at(kSaveMode).offset, at(kContinuation).offset, state, slot(kValue),
        slot(kResult), slot(kResumeMode), at(kThrowBranch).offset, at(kThrow).offset};
    origin.execution = analysis::YieldExecution::kAsynchronous;
    origin.private_registers = analysis::YieldPrivateRegisters{first, kSlots};
    origin.awaited_rejections = analysis::YieldAwaitRejections{value.reject, returned.reject};
    return AsyncGeneratorYield{origin, begin, begin + kSize};
  } catch (Error error) { return error; }
}
}
