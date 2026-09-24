#include "abcd/analysis/accumulator_flow.h"

#include <algorithm>
#include <new>
#include <utility>

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/constant_object.h"
#include "abcd/analysis/constructor_call.h"
#include "abcd/analysis/derived_constructor.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/iter_result.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/ordinary_call.h"
#include "abcd/analysis/rest_arguments.h"

namespace abcd::analysis {
namespace {
enum class AccumulatorWrite { kPreserve, kValue, kHole };
struct Transfer {
  bool reads;
  AccumulatorWrite writes;
};
std::optional<Transfer> Access(const abc::DecodedInstruction& instruction,
                               std::uint64_t frame_size) {
  using abc::Opcode;
  const auto scalar = LiftScalarInstruction(instruction, frame_size);
  const auto created = LiftFunctionCreation(instruction);
  if (const auto* function = std::get_if<FunctionCreation>(&created))
    return Transfer{function->home_object == HomeObjectInput::kAccumulator,
                    AccumulatorWrite::kValue};
  if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar))
    return Transfer{ReadsAccumulator(assignment->expression),
                    std::holds_alternative<Accumulator>(assignment->destination)
                        ? AccumulatorWrite::kValue
                        : AccumulatorWrite::kPreserve};
  const auto constant = LiftConstantObjectInstruction(instruction);
  if (const auto* operation =
          std::get_if<ConstantObjectInstruction>(&constant)) {
    if (operation->kind == ConstantObjectKind::kThis ||
        operation->kind == ConstantObjectKind::kCurrentFunction ||
        operation->kind == ConstantObjectKind::kSymbolConstructor ||
        operation->kind == ConstantObjectKind::kGlobalObject ||
        operation->kind == ConstantObjectKind::kUnmappedArguments ||
        operation->kind == ConstantObjectKind::kAsyncFunctionEnter)
      return Transfer{false, AccumulatorWrite::kValue};
    if (operation->kind == ConstantObjectKind::kDebugger)
      return Transfer{false, AccumulatorWrite::kPreserve};
  }
  if (std::holds_alternative<UnaryExpression>(LiftBranchCondition(instruction)))
    return Transfer{true, AccumulatorWrite::kPreserve};
  if (std::holds_alternative<NamedRead>(LiftNamedRead(instruction)))
    return Transfer{true, AccumulatorWrite::kValue};
  if (std::holds_alternative<NamedWrite>(
          LiftNamedWrite(instruction, frame_size)))
    return Transfer{true, AccumulatorWrite::kPreserve};
  if (std::holds_alternative<OrdinaryCall>(
          LiftOrdinaryCall(instruction, frame_size)))
    return Transfer{true, AccumulatorWrite::kValue};
  if (std::holds_alternative<ReceiverCall>(
          LiftReceiverCall(instruction, frame_size)))
    return Transfer{true, AccumulatorWrite::kValue};
  if (std::holds_alternative<ConstructorCall>(
          LiftConstructorCall(instruction, frame_size)))
    return Transfer{false, AccumulatorWrite::kValue};
  const auto super = LiftSuperInstruction(
      instruction, static_cast<std::uint32_t>(
                       std::min<std::uint64_t>(frame_size, UINT32_MAX)));
  if (std::holds_alternative<SuperRange>(super))
    return Transfer{false, AccumulatorWrite::kValue};
  if (std::holds_alternative<SuperCheck>(super))
    return Transfer{true, AccumulatorWrite::kPreserve};
  const auto global_access = LiftGlobalAccess(instruction);
  if (const auto* global = std::get_if<GlobalAccess>(&global_access);
      global && global->operation == GlobalOp::kReadName)
    return Transfer{false, AccumulatorWrite::kValue};
  if (std::holds_alternative<OwnWrite>(
          LiftOwnWrite(instruction, frame_size)))
    return Transfer{true, AccumulatorWrite::kPreserve};
  const auto object = LiftObjectCreation(instruction);
  if (std::holds_alternative<EmptyObjectCreation>(object) ||
      std::holds_alternative<BufferedObjectCreation>(object))
    return Transfer{false, AccumulatorWrite::kValue};
  if (std::holds_alternative<IterResult>(
          LiftIterResult(instruction, frame_size)))
    return Transfer{false, AccumulatorWrite::kValue};
  if (std::holds_alternative<RestCopy>(LiftRestArguments(instruction)))
    return Transfer{false, AccumulatorWrite::kValue};
  switch (instruction.opcode) {
    case Opcode::kLDHOLE:
      if (instruction.operand_count != 0) return std::nullopt;
      return Transfer{false, AccumulatorWrite::kHole};
    case Opcode::kLDA_STR_ID16:
      if (instruction.operand_count != 1 ||
          instruction.operands[0].kind != abc::OperandKind::kString ||
          instruction.operands[0].raw_bits > UINT16_MAX)
        return std::nullopt;
      return Transfer{false, AccumulatorWrite::kValue};
    case Opcode::kRETURN:
    case Opcode::kTHROW_PREF_NONE:
      return Transfer{true, AccumulatorWrite::kPreserve};
    case Opcode::kNOP:
    case Opcode::kRETURNUNDEFINED:
    case Opcode::kJMP_IMM8:
    case Opcode::kJMP_IMM16:
    case Opcode::kJMP_IMM32:
      return Transfer{false, AccumulatorWrite::kPreserve};
    default:
      return std::nullopt;
  }
}
std::variant<AccumulatorFlow, AccumulatorFailure> AnalyzeAccumulator(
    const abc::MethodBody& body, const NormalFlow& graph,
    std::uint64_t frame_size, AccumulatorBudget budget, bool self_loops,
    std::span<const std::uint8_t> language_loads) {
  if (!body.tries.empty() || !body.handlers.empty())
    return AccumulatorFailure{AccumulatorError::kExceptionRegions, {}};
  if (!language_loads.empty() &&
      language_loads.size() != body.instructions.size())
    return AccumulatorFailure{AccumulatorError::kInvalidGraph, {}};
  std::size_t covered = 0;
  for (const auto& block : graph.blocks) {
    if (block.first_instruction != covered || block.instruction_count == 0 ||
        block.instruction_count > body.instructions.size() - covered)
      return AccumulatorFailure{AccumulatorError::kInvalidGraph, block.begin};
    covered += block.instruction_count;
  }
  if (covered != body.instructions.size())
    return AccumulatorFailure{AccumulatorError::kInvalidGraph, {}};
  try {
    AccumulatorFlow result;
    const auto count = graph.blocks.size();
    if (count > result.entries.max_size() ||
        count > budget.bytes / sizeof(AccumulatorState))
      return AccumulatorFailure{AccumulatorError::kResourceLimit, {}};
    result.entries.resize(count, AccumulatorState::kUnreachable);
    if (result.entries.capacity() > budget.bytes / sizeof(AccumulatorState))
      return AccumulatorFailure{AccumulatorError::kResourceLimit, {}};
    const auto state_bytes =
        result.entries.capacity() * sizeof(AccumulatorState);
    auto ordered =
        self_loops
            ? OrderBlockLoops(graph, {}, {budget.bytes - state_bytes})
            : OrderAcyclicBlocks(graph, {}, {budget.bytes - state_bytes});
    if (const auto* error = std::get_if<OrderError>(&ordered))
      return AccumulatorFailure{AccumulatorError::kOrder, {}, *error};
    result.order = std::get<BlockOrder>(std::move(ordered));
    result.allocated_bytes = state_bytes + result.order.work.allocated_bytes;
    result.entries[0] = AccumulatorState::kMayBeHole;
    for (const auto id : result.order.blocks) {
      auto state = result.entries[id.value];
      const auto& block = graph.blocks[id.value];
      bool repeats = false;
      for (std::size_t i = 0; i < block.successor_count; ++i)
        repeats |= block.successors[i].target == id;
      const auto scan = [&]() -> std::optional<AccumulatorFailure> {
        for (std::size_t i = block.first_instruction;
             i < std::size_t(block.first_instruction) + block.instruction_count;
             ++i) {
          ++result.instruction_visits;
          const auto& instruction = body.instructions[i];
          std::optional<Transfer> transfer;
          if (!language_loads.empty() && language_loads[i]) {
            const auto lexical = LiftLexicalInstruction(instruction.decoded,
                                                        frame_size);
            const auto module = LiftModuleLoad(instruction.decoded);
            if (!std::holds_alternative<LexicalLoad>(lexical) &&
                !std::holds_alternative<ModuleLoad>(module))
              return AccumulatorFailure{
                  AccumulatorError::kUnsupportedInstruction,
                  instruction.offset};
            transfer = Transfer{false, AccumulatorWrite::kValue};
          } else if (i != 0 && !language_loads.empty() &&
                     language_loads[i - 1] &&
                     std::holds_alternative<ModuleLoad>(LiftModuleLoad(
                         body.instructions[i - 1].decoded))) {
            const auto guard = LiftHoleInstruction(instruction.decoded,
                                                   frame_size);
            if (const auto* checked = std::get_if<HoleGuard>(&guard);
                checked &&
                std::holds_alternative<Accumulator>(checked->value) &&
                std::holds_alternative<HoleNameSlot>(checked->name))
              transfer = Transfer{true, AccumulatorWrite::kPreserve};
          }
          if (!transfer) transfer = Access(instruction.decoded, frame_size);
          if (!transfer && i >= std::size_t(block.first_instruction) + 2) {
            const auto local = i - block.first_instruction;
            const auto span =
                std::span(body.instructions)
                    .subspan(block.first_instruction, block.instruction_count);
            for (const auto lookback : {std::size_t{1}, std::size_t{3}}) {
              if (local >= lookback) {
                const auto invocation =
                    MatchNamedInvocation(span, local - lookback, frame_size);
                if (invocation &&
                    invocation->instruction_count == lookback + 1) {
                  transfer = Transfer{true, AccumulatorWrite::kValue};
                  break;
                }
              }
            }
          }
          if (!transfer)
            return AccumulatorFailure{AccumulatorError::kUnsupportedInstruction,
                                      instruction.offset};
          if (transfer->reads && state != AccumulatorState::kValue)
            return AccumulatorFailure{AccumulatorError::kReadBeforeValue,
                                      instruction.offset};
          switch (transfer->writes) {
            case AccumulatorWrite::kPreserve:
              break;
            case AccumulatorWrite::kValue:
              state = AccumulatorState::kValue;
              break;
            case AccumulatorWrite::kHole:
              state = AccumulatorState::kMayBeHole;
              break;
          }
        }
        return {};
      };
      if (const auto error = scan()) return *error;
      // Transfer is preserve or constant write. Only Value -> MayBeHole can
      // weaken entry; one replay checks the resulting fixed point. Never
      // assume an externally initialized accumulator stays initialized forever.
      if (repeats && result.entries[id.value] == AccumulatorState::kValue &&
          state != AccumulatorState::kValue) {
        result.entries[id.value] = AccumulatorState::kMayBeHole;
        state = AccumulatorState::kMayBeHole;
        if (const auto error = scan()) return *error;
      }
      for (std::size_t i = 0; i < block.successor_count; ++i) {
        if (block.successors[i].target == id) continue;
        auto& entry = result.entries[block.successors[i].target.value];
        if (entry == AccumulatorState::kUnreachable)
          entry = state;
        else if (state != AccumulatorState::kValue)
          entry = AccumulatorState::kMayBeHole;
      }
    }
    return result;
  } catch (const std::bad_alloc&) {
    return AccumulatorFailure{AccumulatorError::kResourceLimit, {}};
  }
}
}  // namespace
std::variant<AccumulatorFlow, AccumulatorFailure> AnalyzeAcyclicAccumulator(
    const abc::MethodBody& body, const NormalFlow& graph,
    std::uint64_t frame_size, AccumulatorBudget budget,
    std::span<const std::uint8_t> language_loads) {
  return AnalyzeAccumulator(body, graph, frame_size, budget, false,
                            language_loads);
}
std::variant<AccumulatorFlow, AccumulatorFailure> AnalyzeBlockLoopAccumulator(
    const abc::MethodBody& body, const NormalFlow& graph,
    std::uint64_t frame_size, AccumulatorBudget budget,
    std::span<const std::uint8_t> language_loads) {
  return AnalyzeAccumulator(body, graph, frame_size, budget, true,
                            language_loads);
}
}  // namespace abcd::analysis
