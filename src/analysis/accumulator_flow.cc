#include "abcd/analysis/accumulator_flow.h"

#include <new>
#include <utility>

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/named_read.h"

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
  if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar))
    return Transfer{ReadsAccumulator(assignment->expression),
                    std::holds_alternative<Accumulator>(assignment->destination)
                        ? AccumulatorWrite::kValue
                        : AccumulatorWrite::kPreserve};
  if (std::holds_alternative<UnaryExpression>(LiftBranchCondition(instruction)))
    return Transfer{true, AccumulatorWrite::kPreserve};
  if (std::holds_alternative<NamedRead>(LiftNamedRead(instruction)))
    return Transfer{true, AccumulatorWrite::kValue};
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
}  // namespace
std::variant<AccumulatorFlow, AccumulatorFailure> AnalyzeAcyclicAccumulator(
    const abc::MethodBody& body, const NormalFlow& graph,
    std::uint64_t frame_size, AccumulatorBudget budget) {
  if (!body.tries.empty() || !body.handlers.empty())
    return AccumulatorFailure{AccumulatorError::kExceptionRegions, {}};
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
    auto ordered = OrderAcyclicBlocks(graph, {}, {budget.bytes - state_bytes});
    if (const auto* error = std::get_if<OrderError>(&ordered))
      return AccumulatorFailure{AccumulatorError::kOrder, {}, *error};
    result.order = std::get<BlockOrder>(std::move(ordered));
    result.allocated_bytes = state_bytes + result.order.work.allocated_bytes;
    result.entries[0] = AccumulatorState::kMayBeHole;
    for (const auto id : result.order.blocks) {
      auto state = result.entries[id.value];
      const auto& block = graph.blocks[id.value];
      for (std::size_t i = block.first_instruction;
           i < std::size_t(block.first_instruction) + block.instruction_count;
           ++i) {
        ++result.instruction_visits;
        const auto& instruction = body.instructions[i];
        const auto transfer = Access(instruction.decoded, frame_size);
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
      for (std::size_t i = 0; i < block.successor_count; ++i) {
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
}  // namespace abcd::analysis
