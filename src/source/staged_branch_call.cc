#include "staged_branch_call.h"

#include <algorithm>
#include <ios>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "abcd/abc/control_transfer.h"
#include "branch_staged_calls.h"
#include "branch_value_proof.h"
#include "capture_stream.h"
#include "fresh_object_fields.h"
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
  const auto prefix_end = instructions[call.property].offset.value;
  const auto first_pc = instructions[begin].offset.value;
  const auto end_pc = instructions[call.reload].offset.value;
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    const auto& site = instructions[i];
    const auto transfer = abc::GetControlTransfer(site.decoded.opcode);
    if (!transfer) return Fail(BodyEmitError::kUnsupportedInstruction,
                               site.offset);
    if (i < call.property) {
      if (*transfer != abc::ControlTransfer::kFallthrough &&
          *transfer != abc::ControlTransfer::kJump &&
          *transfer != abc::ControlTransfer::kBranch)
        return Fail(BodyEmitError::kUnsupportedInstruction, site.offset);
      if (site.branch_target &&
          (site.branch_target->value < instructions.front().offset.value ||
           site.branch_target->value > prefix_end))
        return Fail(BodyEmitError::kUnsupportedInstruction, site.offset);
      continue;
    }
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

std::variant<abc::MethodBody, BodyEmitFailure> PrefixBody(
    const abc::MethodBody& body,
    const analysis::StagedNamedInvocation& call, std::size_t bytes) {
  constexpr std::size_t kSyntheticInstructions = 1;
  if (call.property > UINT32_MAX - kSyntheticInstructions ||
      call.property + kSyntheticInstructions >
          bytes / sizeof(abc::BodyInstruction))
    return Fail(BodyEmitError::kResourceLimit);
  try {
    abc::MethodBody result;
    result.instructions.reserve(call.property + kSyntheticInstructions);
    if (result.instructions.capacity() >
        bytes / sizeof(abc::BodyInstruction))
      return Fail(BodyEmitError::kResourceLimit);
    result.instructions.insert(result.instructions.end(),
                               body.instructions.begin(),
                               body.instructions.begin() + call.property);
    abc::DecodedInstruction returned;
    returned.opcode = abc::Opcode::kRETURNUNDEFINED;
    returned.size = kReturnWidth;
    result.instructions.push_back({body.instructions[call.property].offset,
                                   returned, {}});
    return result;
  } catch (const std::bad_alloc&) {
    return Fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(BodyEmitError::kResourceLimit);
  }
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
                        const abc::MethodBody* branch_prefix,
                        const BranchSourceFacts* prefix_facts,
                        std::span<const analysis::StagedNamedInvocation>
                            prefix_calls,
                        std::span<const FreshFieldSite> prefix_fields,
                        AcyclicBudget inner_budget) {
  std::size_t written = 0;
  const auto add = [&](BodyEmitResult result) -> std::optional<BodyEmitFailure> {
    if (const auto* error = std::get_if<BodyEmitFailure>(&result))
      return *error;
    written += std::get<std::size_t>(result);
    return {};
  };
  const auto& sites = body.instructions;
  if (branch_prefix) {
    if (!prefix_facts) return Fail(BodyEmitError::kUnsupportedInstruction);
    const auto write = [&](std::string_view text)
        -> std::optional<BodyEmitFailure> {
      if (text.size() > budget.output.bytes - written)
        return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                               EmitError::kOutputLimit};
      try {
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
      } catch (const std::ios_base::failure&) {
        return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                               EmitError::kWrite};
      }
      if (!output)
        return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                               EmitError::kWrite};
      written += text.size();
      return {};
    };
    if (const auto error = write("$prefix_exit: {\n")) return *error;
    inner_budget.output.bytes = budget.output.bytes - written;
    const auto emitted = WriteCapturedBranchBody(
        *branch_prefix, names, output, inner_budget, constants,
        *prefix_facts, StructuredExit::kPrefixBreak);
    if (!std::holds_alternative<std::size_t>(emitted)) return emitted;
    written += std::get<std::size_t>(emitted);
    if (const auto error = write("}\n")) return *error;
  } else if (const auto error = add(WriteBranchSegment(
                 std::span(sites).first(call.property), 0, false, false,
                 names, output, {budget.output.bytes - written}, constants,
                 outer, prefix_calls, prefix_fields))) {
    return *error;
  }
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
          {budget.output.bytes - written}, constants, outer,
          prefix_calls)))
    return *error;
  return written;
}
}  // namespace

AcyclicResult WriteStagedBranchCall(
    const abc::MethodBody& body, const analysis::StagedNamedInvocation& call,
    ScalarNames names, std::ostream& output, LocalLexicalBudget budget,
    const MethodConstants* constants, const BranchSourceFacts& outer) {
  if (const auto failure = ValidateRegion(body, call)) return *failure;
  const bool branch_prefix = std::any_of(
      body.instructions.begin(), body.instructions.begin() + call.property,
      [](const abc::BodyInstruction& site) {
        return abc::GetControlTransfer(site.decoded.opcode) !=
               abc::ControlTransfer::kFallthrough;
      });
  BranchStagedCalls prefix;
  {
    const auto built = analysis::BuildNormalFlow(body, {budget.bytes});
    if (const auto* error = std::get_if<analysis::FlowFailure>(&built))
      return Fail(error->kind == analysis::FlowError::kResourceLimit
                      ? BodyEmitError::kResourceLimit
                      : BodyEmitError::kUnsupportedInstruction);
    const auto& graph = std::get<analysis::NormalFlow>(built);
    const auto graph_bytes =
        graph.blocks.capacity() * sizeof(analysis::FlowBlock);
    if (graph_bytes > budget.bytes) return Fail(BodyEmitError::kResourceLimit);
    auto matched = BuildBranchStagedCalls(
        body, graph, names.registers.size(), budget.bytes - graph_bytes,
        budget.work / 2);
    if (const auto* error = std::get_if<BodyEmitFailure>(&matched))
      return *error;
    prefix = std::get<BranchStagedCalls>(std::move(matched));
  }
  std::size_t retained = 0;
  for (const auto& nested : prefix.calls) {
    if (nested.property == call.property && nested.call == call.call)
      continue;
    if (nested.call < call.property || nested.property > call.call) {
      prefix.calls[retained++] = nested;
    } else if (nested.property <= call.property ||
               nested.call >= call.call) {
      return Fail(BodyEmitError::kUnsupportedInstruction,
                  body.instructions[nested.property].offset);
    }
  }
  prefix.calls.resize(retained);
  if (prefix.calls.capacity() >
      budget.bytes / sizeof(analysis::StagedNamedInvocation))
    return Fail(BodyEmitError::kResourceLimit);
  budget.bytes -= prefix.calls.capacity() *
                  sizeof(analysis::StagedNamedInvocation);
  budget.work -= budget.work / 2;
  auto planned_fields = branch_prefix
                            ? std::variant<FreshFieldPlan, BodyEmitFailure>{
                                  FreshFieldPlan{}}
                            : BuildFreshFieldPlan(
                                  std::span(body.instructions).first(
                                      call.property),
                                  names.registers.size(), budget.bytes);
  if (const auto* error = std::get_if<BodyEmitFailure>(&planned_fields))
    return *error;
  const auto& fields = std::get<FreshFieldPlan>(planned_fields);
  if (fields.AllocatedBytes() > budget.bytes)
    return Fail(BodyEmitError::kResourceLimit);
  budget.bytes -= fields.AllocatedBytes();
  if (fields.sites.size() > 1) {
    const auto collision = [](std::string_view name) {
      return name.starts_with(kFieldTempPrefix);
    };
    if (collision(names.accumulator) ||
        std::any_of(names.registers.begin(), names.registers.end(),
                    collision))
      return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                             EmitError::kInvalidBinding};
  }
  if (outer.AllocatedBytes() > budget.bytes)
    return Fail(BodyEmitError::kResourceLimit);
  budget.bytes -= outer.AllocatedBytes();
  std::optional<abc::MethodBody> prefix_body;
  std::optional<BranchSourceFacts> prefix_facts;
  if (branch_prefix) {
    auto copied = PrefixBody(body, call, budget.bytes);
    if (const auto* error = std::get_if<BodyEmitFailure>(&copied))
      return *error;
    prefix_body.emplace(std::get<abc::MethodBody>(std::move(copied)));
    const auto prefix_bytes = prefix_body->instructions.capacity() *
                              sizeof(abc::BodyInstruction);
    if (prefix_bytes > budget.bytes)
      return Fail(BodyEmitError::kResourceLimit);
    budget.bytes -= prefix_bytes;
    auto prepared = BuildCapturedBranchFacts(
        *prefix_body, names.registers.size(), constants, budget);
    if (const auto* error = std::get_if<BodyEmitFailure>(&prepared))
      return *error;
    prefix_facts.emplace(std::get<BranchSourceFacts>(std::move(prepared)));
    if (prefix_facts->AllocatedBytes() > budget.bytes)
      return Fail(BodyEmitError::kResourceLimit);
    budget.bytes -= prefix_facts->AllocatedBytes();
    if (const auto failure = ProveBranchValues(
            *prefix_body, names.registers.size(), budget,
            prefix_facts->value_facts))
      return *failure;
  }
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
                                 prefix_body ? &*prefix_body : nullptr,
                                 prefix_facts ? &*prefix_facts : nullptr,
                                 prefix.calls, fields.sites,
                                 inner_budget);
  if (!std::holds_alternative<std::size_t>(checked)) return checked;
  return WritePass(body, argument, call, names, output, budget, constants,
                   outer, inner,
                   prefix_body ? &*prefix_body : nullptr,
                   prefix_facts ? &*prefix_facts : nullptr,
                   prefix.calls, fields.sites, inner_budget);
}
}  // namespace abcd::source
