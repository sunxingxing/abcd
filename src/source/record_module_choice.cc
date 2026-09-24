#include "record_module_choice.h"

#include <span>
#include <variant>

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/module_store.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/scalar.h"

namespace abcd::source {
std::optional<RecordModuleChoice> MatchRecordModuleChoice(
    const abc::MethodBody& body, std::size_t first,
    std::uint64_t frame_size) {
  constexpr std::size_t kInstructionCount = 7;
  constexpr std::size_t kTest = 0;
  constexpr std::size_t kBranch = 1;
  constexpr std::size_t kObject = 2;
  constexpr std::size_t kStore = 3;
  constexpr std::size_t kJump = 4;
  constexpr std::size_t kPrior = 5;
  constexpr std::size_t kJoin = 6;
  if (first > body.instructions.size() ||
      body.instructions.size() - first < kInstructionCount)
    return {};
  const auto sites = std::span(body.instructions).subspan(
      first, kInstructionCount);
  using abc::Opcode;
  const auto test_opcode = sites[kTest].decoded.opcode;
  if (test_opcode != Opcode::kCALLRUNTIME_ISTRUE_PREF_IMM8 &&
      test_opcode != Opcode::kISTRUE)
    return {};
  const auto test = analysis::LiftScalarInstruction(
      sites[kTest].decoded, frame_size);
  const auto* normalized = std::get_if<analysis::ScalarAssignment>(&test);
  const auto* boolean = normalized
                            ? std::get_if<analysis::UnaryExpression>(
                                  &normalized->expression)
                            : nullptr;
  if (!boolean ||
      !std::holds_alternative<analysis::Accumulator>(
          normalized->destination) ||
      normalized->completion != analysis::CompletionEffect::kNormal ||
      boolean->op != analysis::UnaryOperator::kToBoolean ||
      !std::holds_alternative<analysis::Accumulator>(boolean->input))
    return {};
  const auto branch = analysis::LiftBranchCondition(sites[kBranch].decoded);
  const auto* condition = std::get_if<analysis::UnaryExpression>(&branch);
  if (!condition ||
      condition->op != analysis::UnaryOperator::kTrueOrNonzeroNumber ||
      !sites[kBranch].branch_target ||
      *sites[kBranch].branch_target != sites[kPrior].offset)
    return {};
  if (!std::holds_alternative<analysis::EmptyObjectCreation>(
          analysis::LiftObjectCreation(sites[kObject].decoded)))
    return {};
  const auto store = analysis::LiftModuleStore(sites[kStore].decoded);
  const auto* write = std::get_if<analysis::ModuleStore>(&store);
  if (!write) return {};
  const auto jump_opcode = sites[kJump].decoded.opcode;
  if (jump_opcode != Opcode::kJMP_IMM8 &&
      jump_opcode != Opcode::kJMP_IMM16 &&
      jump_opcode != Opcode::kJMP_IMM32)
    return {};
  if (!sites[kJump].branch_target ||
      *sites[kJump].branch_target != sites[kJoin].offset ||
      !sites[kJump].decoded.jump || sites[kJump].decoded.conditional)
    return {};
  const auto prior = analysis::LiftScalarInstruction(
      sites[kPrior].decoded, frame_size);
  const auto* load = std::get_if<analysis::ScalarAssignment>(&prior);
  const auto* prior_value = load
                                ? std::get_if<analysis::ScalarValue>(
                                      &load->expression)
                                : nullptr;
  const auto* prior_register = prior_value
                                   ? std::get_if<abc::RegisterId>(prior_value)
                                   : nullptr;
  if (sites[kPrior].decoded.opcode != Opcode::kLDA_V8 || !prior_register ||
      !std::holds_alternative<analysis::Accumulator>(load->destination))
    return {};
  const auto joined = analysis::LiftScalarInstruction(
      sites[kJoin].decoded, frame_size);
  const auto* assign = std::get_if<analysis::ScalarAssignment>(&joined);
  const auto* destination = assign
                                ? std::get_if<abc::RegisterId>(
                                      &assign->destination)
                                : nullptr;
  const auto* joined_value = assign
                                 ? std::get_if<analysis::ScalarValue>(
                                       &assign->expression)
                                 : nullptr;
  if (sites[kJoin].decoded.opcode != Opcode::kSTA_V8 || !destination ||
      !joined_value ||
      !std::holds_alternative<analysis::Accumulator>(*joined_value))
    return {};
  return RecordModuleChoice{write->slot, *prior_register, *destination,
                            sites[kStore].offset, first + kInstructionCount};
}
}  // namespace abcd::source
