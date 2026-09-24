#include "structured_catch.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/lexical.h"
#include "branch_staged_calls.h"
#include "branch_source_facts.h"
#include "branch_value_proof.h"
#include "catch_register_liveness.h"

namespace abcd::source {
namespace {
constexpr std::uint8_t kReturnWidth = 1;  // Pinned isa.yaml RETURNUNDEFINED.
constexpr std::size_t kCatchWrapperNesting = 3;
struct CatchShape {
  std::size_t try_begin;
  std::size_t try_end;
  std::size_t handler_begin;
  std::size_t handler_end;
  abc::RegisterId caught;
};
struct Segment {
  std::size_t begin;
  std::size_t end;
  bool synthetic;
  StructuredExit exit;
};
BodyEmitFailure Fail(BodyEmitError kind, abc::CodeOffset offset = {}) {
  return {kind, offset};
}
BodyEmitFailure Rebase(BodyEmitFailure failure, std::uint32_t base) {
  failure.offset.value += base;
  return failure;
}
std::optional<std::size_t> At(const abc::MethodBody& body,
                              std::uint32_t offset) {
  const auto& sites = body.instructions;
  const auto found = std::lower_bound(
      sites.begin(), sites.end(), abc::CodeOffset{offset},
      [](const abc::BodyInstruction& site, abc::CodeOffset pc) {
        return site.offset < pc;
      });
  if (found == sites.end() || found->offset.value != offset) return {};
  return static_cast<std::size_t>(found - sites.begin());
}
std::variant<CatchShape, BodyEmitFailure> ProveShape(
    const abc::MethodBody& body, std::uint64_t frame_size) {
  using abc::ControlTransfer;
  if (body.tries.size() != 1 || body.handlers.size() != 1 ||
      body.instructions.empty())
    return Fail(BodyEmitError::kExceptionRegions);
  const auto& region = body.tries[0];
  const auto& handler = body.handlers[0];
  const auto try_end_pc = std::uint64_t(region.offset.value) + region.length;
  const auto handler_end_pc =
      std::uint64_t(handler.offset.value) + handler.length;
  if (region.first_handler != 0 || region.handler_count != 1 || handler.type ||
      region.length == 0 || handler.length == 0 ||
      try_end_pc >= handler.offset.value ||
      handler_end_pc > std::numeric_limits<std::uint32_t>::max())
    return Fail(BodyEmitError::kExceptionRegions);
  const auto start = At(body, region.offset.value);
  const auto end = At(body, static_cast<std::uint32_t>(try_end_pc));
  const auto catch_start = At(body, handler.offset.value);
  const auto catch_end = At(body, static_cast<std::uint32_t>(handler_end_pc));
  if (!start || !end || !catch_start || !catch_end ||
      *start >= *end || *end + 1 != *catch_start ||
      *catch_start + 1 >= *catch_end)
    return Fail(BodyEmitError::kExceptionRegions);
  const auto& skip = body.instructions[*end];
  const auto transfer = abc::GetControlTransfer(skip.decoded.opcode);
  if (!transfer || *transfer != ControlTransfer::kJump ||
      !skip.branch_target || skip.branch_target->value != handler_end_pc ||
      std::uint64_t(skip.offset.value) + skip.decoded.size !=
          handler.offset.value)
    return Fail(BodyEmitError::kExceptionRegions, skip.offset);
  const auto& caught = body.instructions[*catch_start].decoded;
  if (caught.opcode != abc::Opcode::kSTA_V8 || caught.operand_count != 1 ||
      caught.operands[0].kind != abc::OperandKind::kRegister ||
      caught.operands[0].raw_bits >= frame_size)
    return Fail(BodyEmitError::kExceptionRegions,
                body.instructions[*catch_start].offset);
  // Segment boundaries are only normal exits, never alternative entries.
  const auto enclosed = [&](std::size_t begin, std::size_t end,
                            std::uint32_t boundary, bool allow_end) {
    const auto first_pc = body.instructions[begin].offset.value;
    for (std::size_t i = begin; i < end; ++i) {
      const auto target = body.instructions[i].branch_target;
      if (target && (target->value < first_pc ||
                     target->value > boundary ||
                     (!allow_end && target->value == boundary)))
        return false;
    }
    return true;
  };
  const auto& sites = body.instructions;
  const auto code_end = std::uint64_t(sites.back().offset.value) +
                        sites.back().decoded.size;
  if (code_end > std::numeric_limits<std::uint32_t>::max() ||
      !enclosed(0, *start, region.offset.value, true) ||
      !enclosed(*start, *end, static_cast<std::uint32_t>(try_end_pc), true) ||
      !enclosed(*catch_start + 1, *catch_end,
                static_cast<std::uint32_t>(handler_end_pc), true) ||
      !enclosed(*catch_end, sites.size(), static_cast<std::uint32_t>(code_end),
                false))
    return Fail(BodyEmitError::kExceptionRegions);
  // Rebased segments cannot reconstruct an environment created outside them.
  for (const auto& site : sites) {
    const auto lexical = analysis::LiftLexicalInstruction(site.decoded,
                                                           frame_size);
    if (!std::holds_alternative<analysis::LexicalError>(lexical) ||
        std::get<analysis::LexicalError>(lexical) !=
            analysis::LexicalError::kUnsupportedOpcode)
      return Fail(BodyEmitError::kExceptionRegions, site.offset);
  }
  return CatchShape{*start, *end, *catch_start, *catch_end,
                    {static_cast<std::uint32_t>(caught.operands[0].raw_bits)}};
}

std::variant<abc::MethodBody, BodyEmitFailure> BuildSegment(
    const abc::MethodBody& body, Segment range, std::size_t bytes) {
  if (range.synthetic != (range.exit != StructuredExit::kReturn))
    return Fail(BodyEmitError::kExceptionRegions);
  const auto& sites = body.instructions;
  const auto count = range.end - range.begin;
  if (count + range.synthetic > bytes / sizeof(abc::BodyInstruction))
    return Fail(BodyEmitError::kResourceLimit);
  try {
    abc::MethodBody result;
    result.instructions.reserve(count + range.synthetic);
    if (result.instructions.capacity() >
        bytes / sizeof(abc::BodyInstruction))
      return Fail(BodyEmitError::kResourceLimit);
    const auto base = range.begin < sites.size()
                          ? sites[range.begin].offset.value
                          : std::uint32_t{0};
    const auto end_pc = range.end < sites.size()
                            ? sites[range.end].offset.value
                            : std::uint64_t(sites.back().offset.value) +
                                  sites.back().decoded.size;
    for (std::size_t i = range.begin; i < range.end; ++i) {
      auto site = sites[i];
      site.offset.value -= base;
      if (site.branch_target) site.branch_target->value -= base;
      result.instructions.push_back(std::move(site));
    }
    if (range.synthetic) {
      if (end_pc < base || end_pc - base >=
                               std::numeric_limits<std::uint32_t>::max())
        return Fail(BodyEmitError::kResourceLimit);
      abc::DecodedInstruction returned;
      returned.opcode = abc::Opcode::kRETURNUNDEFINED;
      returned.size = kReturnWidth;
      result.instructions.push_back(
          {{static_cast<std::uint32_t>(end_pc - base)}, returned, {}});
    }
    return result;
  } catch (const std::bad_alloc&) {
    return Fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(BodyEmitError::kResourceLimit);
  }
}

FunctionEmitResult WriteSegment(const abc::MethodBody& original,
                                Segment range, ScalarNames names,
                                std::ostream& output,
                                LocalLexicalBudget budget,
                                const MethodConstants* constants,
                                const CatchRegisterLiveness& liveness) {
  if (liveness.AllocatedBytes() > budget.bytes)
    return FunctionEmitError::kResourceLimit;
  budget.bytes -= liveness.AllocatedBytes();
  const auto built = BuildSegment(original, range, budget.bytes);
  if (const auto* error = std::get_if<BodyEmitFailure>(&built)) return *error;
  const auto& body = std::get<abc::MethodBody>(built);
  const auto base = original.instructions[range.begin].offset.value;
  const auto copy_bytes = body.instructions.capacity() *
                          sizeof(abc::BodyInstruction);
  budget.bytes -= copy_bytes;
  const auto prepared = BuildCapturedBranchFacts(
      body, names.registers.size(), constants, budget);
  if (const auto* error = std::get_if<BodyEmitFailure>(&prepared))
    return Rebase(*error, base);
  const auto& facts = std::get<BranchSourceFacts>(prepared);
  if (facts.AllocatedBytes() > budget.bytes)
    return FunctionEmitError::kResourceLimit;
  auto proof_budget = budget;
  proof_budget.bytes -= facts.AllocatedBytes();
  if (const auto unsafe = ProveBranchValues(
          body, names.registers.size(), proof_budget, facts.value_facts))
    return Rebase(*unsafe, base);
  {
    const auto built_graph = analysis::BuildNormalFlow(
        body, {proof_budget.bytes});
    if (const auto* error = std::get_if<analysis::FlowFailure>(&built_graph)) {
      auto failure = *error;
      failure.offset.value += base;
      return failure;
    }
    const auto& graph = std::get<analysis::NormalFlow>(built_graph);
    const auto graph_bytes = graph.blocks.capacity() *
                             sizeof(analysis::FlowBlock);
    if (graph_bytes > proof_budget.bytes)
      return FunctionEmitError::kResourceLimit;
    const auto planned = BuildBranchStagedCalls(
        body, graph, names.registers.size(), proof_budget.bytes - graph_bytes,
        proof_budget.work);
    if (const auto* error = std::get_if<BodyEmitFailure>(&planned))
      return Rebase(*error, base);
    for (const auto& call : std::get<BranchStagedCalls>(planned).calls) {
      const auto original_call = range.begin + call.call;
      if (!liveness.DeadAfter(original_call, call.callee))
        return Fail(BodyEmitError::kUnsupportedInstruction,
                    original.instructions[original_call].offset);
    }
  }
  AcyclicBudget ordered;
  ordered.bytes = budget.bytes;
  ordered.work = budget.work;
  // The same segment-local candidates were checked against exceptional
  // liveness before enabling fusion in the ordered writer.
  ordered.allow_staged_calls = true;
  if (ordered.max_nesting < kCatchWrapperNesting)
    return FunctionEmitError::kResourceLimit;
  ordered.max_nesting -= kCatchWrapperNesting;
  ordered.output = budget.output;
  auto emitted = std::visit(
      [](auto value) -> FunctionEmitResult { return value; },
      WriteCapturedBranchBody(body, names, output, ordered, constants, facts,
                              range.exit));
  std::visit([&](auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, BodyEmitFailure> ||
                  std::is_same_v<T, analysis::FlowFailure> ||
                  std::is_same_v<T, analysis::AccumulatorFailure>)
      value.offset.value += base;
  }, emitted);
  return emitted;
}
}  // namespace

FunctionEmitResult WriteStructuredCatch(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    LocalLexicalBudget budget, const MethodConstants* constants) {
  const auto proved = ProveShape(body, names.registers.size());
  if (const auto* error = std::get_if<BodyEmitFailure>(&proved)) return *error;
  const auto shape = std::get<CatchShape>(proved);
  if (names.accumulator == "$caught" ||
      std::find(names.registers.begin(), names.registers.end(), "$caught") !=
          names.registers.end())
    return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                           EmitError::kInvalidBinding};
  const auto analyzed = CatchRegisterLiveness::Build(
      body, shape.try_begin, shape.try_end, shape.handler_begin,
      names.registers.size(), budget.bytes, budget.work);
  if (const auto* error = std::get_if<BodyEmitFailure>(&analyzed))
    return *error;
  const auto& liveness = std::get<CatchRegisterLiveness>(analyzed);
  std::size_t written = 0;
  const auto append = [&](std::string_view text) -> std::optional<EmitError> {
    if (text.size() > budget.output.bytes - written)
      return EmitError::kOutputLimit;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) return EmitError::kWrite;
    written += text.size();
    return {};
  };
  const auto emit = [&](Segment segment) -> FunctionEmitResult {
    auto remaining = budget;
    remaining.output.bytes -= written;
    const auto result = WriteSegment(body, segment, names, output, remaining,
                                     constants, liveness);
    if (const auto* size = std::get_if<std::size_t>(&result)) written += *size;
    return result;
  };
  if (const auto error = append("$prefix_exit: {\n")) return *error;
  if (const auto result = emit({0, shape.try_begin, true,
                               StructuredExit::kPrefixBreak});
      !std::holds_alternative<std::size_t>(result)) return result;
  if (const auto error = append("}\ntry {\n$try_exit: {\n")) return *error;
  if (const auto result = emit({shape.try_begin, shape.try_end, true,
                               StructuredExit::kTryBreak});
      !std::holds_alternative<std::size_t>(result)) return result;
  if (const auto error = append("}\n} catch ($caught) {\n$acc = $caught;\n"))
    return *error;
  if (const auto error = append(names.registers[shape.caught.value]))
    return *error;
  if (const auto error = append(" = $caught;\n$handler_exit: {\n"))
    return *error;
  if (const auto result = emit({shape.handler_begin + 1, shape.handler_end,
                               true, StructuredExit::kHandlerBreak});
      !std::holds_alternative<std::size_t>(result)) return result;
  if (const auto error = append("}\n}\n")) return *error;
  const auto suffix = emit({shape.handler_end, body.instructions.size(), false,
                            StructuredExit::kReturn});
  if (!std::holds_alternative<std::size_t>(suffix)) return suffix;
  return written;
}
}  // namespace abcd::source
