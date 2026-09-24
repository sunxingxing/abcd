#include "staged_branch_call.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "abcd/abc/control_transfer.h"
#include "branch_value_proof.h"
#include "capture_stream.h"
#include "linear_instruction.h"
#include "ordered_body.h"

namespace abcd::source {
namespace {
constexpr std::uint8_t kReturnWidth = 1;  // Pinned isa.yaml RETURN format.
BodyEmitFailure Fail(BodyEmitError kind, abc::CodeOffset offset = {}) {
  return {kind, offset};
}
std::optional<BodyEmitFailure> ValidateRegion(
    const abc::MethodBody& body,
    const analysis::StagedNamedInvocation& call) {
  const auto& instructions = body.instructions;
  if (!body.tries.empty() || !body.handlers.empty() ||
      call.timing != analysis::SpilledCallTiming::kDeferredArgument ||
      call.invoke.Count() == 0 || call.property == 0 ||
      call.property + 1 != call.spill || call.spill + 1 >= call.reload ||
      call.reload + 1 != call.call || call.call + 1 >= instructions.size())
    return Fail(BodyEmitError::kUnsupportedInstruction);
  const auto begin = call.spill + 1;
  const auto first_pc = instructions[begin].offset.value;
  const auto end_pc = instructions[call.reload].offset.value;
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    const auto& site = instructions[i];
    const auto transfer = abc::GetControlTransfer(site.decoded.opcode);
    if (!transfer) return Fail(BodyEmitError::kUnsupportedInstruction,
                               site.offset);
    if (i < begin || i >= call.reload) {
      if (i + 1 == instructions.size()) {
        if (*transfer != abc::ControlTransfer::kReturn &&
            *transfer != abc::ControlTransfer::kThrow)
          return Fail(BodyEmitError::kUnsupportedInstruction, site.offset);
      } else if (*transfer != abc::ControlTransfer::kFallthrough ||
                 site.branch_target) {
        return Fail(BodyEmitError::kUnsupportedInstruction, site.offset);
      }
      continue;
    }
    if (*transfer != abc::ControlTransfer::kFallthrough &&
        *transfer != abc::ControlTransfer::kJump &&
        *transfer != abc::ControlTransfer::kBranch)
      return Fail(BodyEmitError::kUnsupportedInstruction, site.offset);
    if (site.branch_target &&
        (site.branch_target->value < first_pc ||
         site.branch_target->value > end_pc))
      return Fail(BodyEmitError::kUnsupportedInstruction, site.offset);
  }
  const auto& reload = instructions[call.reload].decoded;
  const auto argument = call.invoke.At(0);
  if (!argument || argument->value > UINT8_MAX ||
      reload.opcode != abc::Opcode::kLDA_V8 || reload.operand_count != 1 ||
      reload.operands[0].kind != abc::OperandKind::kRegister)
    return Fail(BodyEmitError::kUnsupportedInstruction,
                instructions[call.reload].offset);
  return {};
}

std::variant<abc::MethodBody, BodyEmitFailure> ArgumentBody(
    const abc::MethodBody& body,
    const analysis::StagedNamedInvocation& call, std::size_t bytes) {
  const auto begin = call.spill + 1;
  const auto end = call.reload;
  const auto count = end - begin;
  constexpr std::size_t kSyntheticInstructions = 2;
  if (count > UINT32_MAX - kSyntheticInstructions ||
      count + kSyntheticInstructions > bytes / sizeof(abc::BodyInstruction))
    return Fail(BodyEmitError::kResourceLimit);
  try {
    abc::MethodBody result;
    result.instructions.reserve(count + kSyntheticInstructions);
    if (result.instructions.capacity() >
        bytes / sizeof(abc::BodyInstruction))
      return Fail(BodyEmitError::kResourceLimit);
    const auto base = body.instructions[begin].offset.value;
    for (std::size_t i = begin; i < end; ++i) {
      auto site = body.instructions[i];
      site.offset.value -= base;
      if (site.branch_target) site.branch_target->value -= base;
      result.instructions.push_back(std::move(site));
    }
    auto argument = body.instructions[call.reload];
    argument.offset.value -= base;
    argument.decoded.operands[0].raw_bits = call.invoke.At(0)->value;
    result.instructions.push_back(std::move(argument));
    const auto return_pc =
        std::uint64_t(result.instructions.back().offset.value) +
        result.instructions.back().decoded.size;
    if (return_pc >= std::numeric_limits<std::uint32_t>::max())
      return Fail(BodyEmitError::kResourceLimit);
    abc::DecodedInstruction returned;
    returned.opcode = abc::Opcode::kRETURN;
    returned.size = kReturnWidth;
    returned.accumulator_read = true;
    result.instructions.push_back(
        {{static_cast<std::uint32_t>(return_pc)}, returned, {}});
    return result;
  } catch (const std::bad_alloc&) {
    return Fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(BodyEmitError::kResourceLimit);
  }
}

AcyclicResult WritePass(const abc::MethodBody& body,
                        const abc::MethodBody& argument,
                        const analysis::StagedNamedInvocation& call,
                        ScalarNames names, std::ostream& output,
                        LocalLexicalBudget budget,
                        const MethodConstants* constants,
                        const BranchSourceFacts& outer,
                        const BranchSourceFacts& inner,
                        AcyclicBudget inner_budget) {
  std::size_t written = 0;
  const auto add = [&](BodyEmitResult result) -> std::optional<BodyEmitFailure> {
    if (const auto* error = std::get_if<BodyEmitFailure>(&result))
      return *error;
    written += std::get<std::size_t>(result);
    return {};
  };
  const auto& sites = body.instructions;
  if (const auto error = add(WriteBranchSegment(
          std::span(sites).first(call.property), 0, false, false, names,
          output, {budget.output.bytes - written}, constants, outer)))
    return *error;
  if (const auto error = add(WriteStagedCallHead(
          call, sites[call.property].offset, names, output,
          {budget.output.bytes - written}, constants)))
    return *error;
  inner_budget.output.bytes = budget.output.bytes - written;
  const auto emitted = WriteCapturedBranchBody(
      argument, names, output, inner_budget, constants, inner);
  if (!std::holds_alternative<std::size_t>(emitted)) return emitted;
  written += std::get<std::size_t>(emitted);
  if (const auto error = add(WriteStagedCallClose(
          call, sites[call.call].offset, names, output,
          {budget.output.bytes - written})))
    return *error;
  if (const auto error = add(WriteBranchSegment(
          std::span(sites).subspan(call.call + 1), call.call + 1,
          true, true, names, output,
          {budget.output.bytes - written}, constants, outer)))
    return *error;
  return written;
}
}  // namespace

AcyclicResult WriteStagedBranchCall(
    const abc::MethodBody& body, const analysis::StagedNamedInvocation& call,
    ScalarNames names, std::ostream& output, LocalLexicalBudget budget,
    const MethodConstants* constants, const BranchSourceFacts& outer) {
  if (const auto failure = ValidateRegion(body, call)) return *failure;
  if (outer.AllocatedBytes() > budget.bytes)
    return Fail(BodyEmitError::kResourceLimit);
  budget.bytes -= outer.AllocatedBytes();
  auto built = ArgumentBody(body, call, budget.bytes);
  if (const auto* error = std::get_if<BodyEmitFailure>(&built)) return *error;
  const auto& argument = std::get<abc::MethodBody>(built);
  const auto body_bytes = argument.instructions.capacity() *
                          sizeof(abc::BodyInstruction);
  if (body_bytes > budget.bytes)
    return Fail(BodyEmitError::kResourceLimit);
  budget.bytes -= body_bytes;
  const auto prepared = BuildCapturedBranchFacts(
      argument, names.registers.size(), constants, budget);
  if (const auto* error = std::get_if<BodyEmitFailure>(&prepared))
    return *error;
  const auto& inner = std::get<BranchSourceFacts>(prepared);
  if (inner.AllocatedBytes() > budget.bytes)
    return Fail(BodyEmitError::kResourceLimit);
  auto proof_budget = budget;
  proof_budget.bytes -= inner.AllocatedBytes();
  if (const auto failure = ProveBranchValues(
          argument, names.registers.size(), proof_budget,
          inner.value_facts))
    return *failure;
  AcyclicBudget inner_budget;
  inner_budget.bytes = budget.bytes;
  CaptureDiscard buffer;
  std::ostream discard(&buffer);
  const auto checked = WritePass(body, argument, call, names, discard,
                                 budget, constants, outer, inner,
                                 inner_budget);
  if (!std::holds_alternative<std::size_t>(checked)) return checked;
  return WritePass(body, argument, call, names, output, budget, constants,
                   outer, inner, inner_budget);
}
}  // namespace abcd::source
