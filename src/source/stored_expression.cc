#include "stored_expression.h"

#include "expression_origins.h"
#include "private_copy.h"
#include "abcd/source/source_contract.h"

namespace abcd::source {
namespace {
bool HasMergedOrigin(const SourceStatement& statement) {
  return statement.fused_read || statement.fused_spill || statement.fused_reload ||
         statement.class_origin || statement.previous_origin;
}
bool IsAccumulator(const analysis::ScalarExpression& expression) {
  const auto* value = std::get_if<analysis::ScalarValue>(&expression);
  return value && std::holds_alternative<analysis::Accumulator>(*value);
}
bool NeedsAggregateFold(const SourceExpression& expression) {
  if (std::holds_alternative<ClassExpression>(expression) ||
      std::holds_alternative<analysis::BufferedArrayCreation>(expression) ||
      std::holds_alternative<analysis::BufferedObjectCreation>(expression)) return true;
  const auto* scalar = std::get_if<analysis::ScalarExpression>(&expression);
  const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
  return value && std::holds_alternative<analysis::EmptyArray>(*value);
}
bool IsRegisterReload(const SourceExpression& expression,
                      const analysis::SemanticInstruction& instruction,
                      abc::RegisterId target) {
  const auto* scalar = std::get_if<analysis::ScalarExpression>(&expression);
  const auto* value = scalar ? std::get_if<analysis::ScalarValue>(scalar) : nullptr;
  const auto* reg = value ? std::get_if<abc::RegisterId>(value) : nullptr;
  const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
  const auto* input = copy
      ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
  const auto* original = input ? std::get_if<abc::RegisterId>(input) : nullptr;
  return reg && *reg == target && original && *original == target &&
         std::holds_alternative<analysis::Accumulator>(copy->destination) &&
         copy->completion == analysis::CompletionEffect::kNormal &&
         IsPureCopyStep(instruction);
}
bool FoldFieldValue(const AstMethod& method, std::size_t position,
                     std::size_t store_position,
                     SourceStatement& head, SourceStatement& tail,
                     std::span<const std::size_t> uses) {
  const auto* assignment = std::get_if<SourceAssignment>(&head.operation);
  auto* member = std::get_if<SourceMemberStore>(&tail.operation);
  auto* own = std::get_if<SourceOwnStore>(&tail.operation);
  auto* field_value = member ? &member->value : own ? &own->value : nullptr;
  const auto* member_value = field_value
      ? std::get_if<analysis::ScalarValue>(field_value) : nullptr;
  if (!assignment || !field_value || HasMergedOrigin(head) || HasMergedOrigin(tail) ||
      !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
      !member_value ||
      !std::holds_alternative<analysis::Accumulator>(*member_value)) return false;
  const auto* expression = std::get_if<analysis::ScalarExpression>(&assignment->value);
  const auto* value = expression ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
  const auto* string = std::get_if<StringExpression>(&assignment->value);
  const bool array = value && std::holds_alternative<analysis::EmptyArray>(*value);
  if ((!value && !string) ||
      (array && ResourceFailures(kSourceContract) != ResourceFailureScope::kExcluded))
    return false;
  const auto& producer = method.instructions[position];
  const auto* scalar = std::get_if<analysis::ScalarAssignment>(&producer.operation);
  const auto* original_value = scalar
      ? std::get_if<analysis::ScalarValue>(&scalar->expression) : nullptr;
  if (string) {
    const auto* load = std::get_if<analysis::StringLoad>(&producer.operation);
    if (!load || load->value != string->value) return false;
  } else if (!original_value ||
             std::holds_alternative<analysis::EmptyArray>(*original_value) != array ||
             !std::holds_alternative<analysis::Accumulator>(scalar->destination) ||
             scalar->completion != analysis::CompletionEffect::kNormal) {
    return false;
  }
  const auto* store = std::get_if<analysis::NamedWrite>(
      &method.instructions[store_position].operation);
  const auto* define = std::get_if<analysis::OwnWrite>(
      &method.instructions[store_position].operation);
  const auto* receiver = member ? std::get_if<abc::RegisterId>(&member->object)
                                : &own->write.object;
  const bool same_store = member
      ? store && receiver && *receiver == store->object && member->name == store->name
      : define && own->write.kind == define->kind &&
            own->write.object == define->object && own->write.name == define->name;
  const auto& written = producer.effects.normal_registers.write;
  const auto allowed_effect = array ? analysis::LanguageEffect::kAllocation
                                   : analysis::LanguageEffect::kNone;
  if (!written || !std::holds_alternative<analysis::Accumulator>(*written) ||
      producer.effects.normal != allowed_effect ||
      producer.effects.before_throw != analysis::LanguageEffect::kNone ||
      producer.effects.exceptional != analysis::ExceptionalCompletion::kImpossible ||
      !same_store)
    return false;
  const auto output = method.ssa.Write({static_cast<std::uint32_t>(position)});
  if (!output || uses[output->value] != 1) return false;
  const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(store_position)});
  bool consumed = false;
  for (const auto& read : reads) consumed |= read.value == *output;
  if (!consumed) return false;
  // Private reads, constants or admitted empty arrays cross receiver/key
  // evaluation. The named write stays at its original PC with all setter/Proxy
  // effects. Exact uses include handler phis observing the removed acc write.
  // Empty-array allocation crosses only a private receiver/key evaluation,
  // never a getter or setter. Resource failure ordering is excluded by V2;
  // the fresh array is still allocated once before the original field write.
  // Own-store origin/descriptor verification remains mandatory at emission.
  if (string) *field_value = *string;
  else *field_value = *value;
  head.operation = analysis::NoOperation{};
  return true;
}
}  // namespace

void FoldStoredExpressions(const AstMethod& method, SourceAst& ast,
                           std::span<const std::size_t> uses) {
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t i = block.first_instruction; i + 1 < end; ++i) {
      auto& head = ast.statements[i];
      auto& tail = ast.statements[i + 1];
      if (std::holds_alternative<SourceAssignment>(head.operation)) {
        auto field = i + 1;
        // Only an emitted producer starts this scan, so skipped NoOp intervals
        // are disjoint. Their original copies must be pure; the final store
        // must still read the producer's exact SSA value, not a reload alias.
        for (; field < end; ++field) {
          const auto& skipped = ast.statements[field];
          const auto& instruction = method.instructions[field];
          const auto* copy = std::get_if<analysis::ScalarAssignment>(&instruction.operation);
          const auto* input = copy
              ? std::get_if<analysis::ScalarValue>(&copy->expression) : nullptr;
          if (!std::holds_alternative<analysis::NoOperation>(skipped.operation) ||
              HasMergedOrigin(skipped) || !copy ||
              copy->completion != analysis::CompletionEffect::kNormal ||
              !input || (!std::holds_alternative<analysis::Accumulator>(*input) &&
                         !std::holds_alternative<abc::RegisterId>(*input)) ||
              !IsPureCopyStep(instruction)) break;
        }
        if (field < end && FoldFieldValue(method, i, field, head,
                                           ast.statements[field], uses)) {
          i = field;
          continue;
        }
      }
      auto* assignment = std::get_if<SourceAssignment>(&head.operation);
      const auto* save = std::get_if<SourceAssignment>(&tail.operation);
      const auto* local = std::get_if<SourceLocalStore>(&tail.operation);
      if (!assignment || (!save && !local) || HasMergedOrigin(tail) ||
          !CanRetargetExpression(head, assignment->value) ||
          !std::holds_alternative<analysis::Accumulator>(assignment->target) ||
          NeedsAggregateFold(assignment->value)) continue;
      std::optional<abc::RegisterId> destination;
      const auto& original = method.instructions[i + 1];
      if (save) {
        const auto* reg = std::get_if<abc::RegisterId>(&save->target);
        const auto* input = std::get_if<analysis::ScalarExpression>(&save->value);
        const auto* copy = std::get_if<analysis::ScalarAssignment>(&original.operation);
        if (!reg || !input || !IsAccumulator(*input) || !copy ||
            copy->completion != analysis::CompletionEffect::kNormal ||
            !IsAccumulator(copy->expression) ||
            !std::holds_alternative<abc::RegisterId>(copy->destination) ||
            std::get<abc::RegisterId>(copy->destination) != *reg) continue;
        destination = *reg;
      } else {
        const auto* store = std::get_if<analysis::LexicalStore>(&original.operation);
        if (!store || !std::holds_alternative<analysis::Accumulator>(store->value) ||
            !std::holds_alternative<analysis::Accumulator>(local->value) ||
            original.effects.exceptional != analysis::ExceptionalCompletion::kImpossible)
          continue;
      }
      const auto& write = method.instructions[i].effects.normal_registers.write;
      if (!write || !std::holds_alternative<analysis::Accumulator>(*write)) continue;
      const auto value = method.ssa.Write({static_cast<std::uint32_t>(i)});
      const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(i + 1)});
      if (!value || uses[value->value] != 1 || reads.size() != 1 ||
          reads.front().value != *value) continue;
      // RHS evaluation, including throws and creation captures, stays at its
      // original PC. The destination still commits only after normal return.
      // No other observer sees the removed acc value, including handler phis.
      if (destination) {
        if (IsRegisterReload(assignment->value, method.instructions[i], *destination) &&
            IsPureCopyStep(original)) {
          // Exact adjacent acc transport back into its source register cannot
          // change the private value. The acc has no other use (including phi
          // observers). Keep both PC anchors; do not rewire SSA or use counts.
          head.operation = analysis::NoOperation{};
        } else {
          assignment->target = *destination;
        }
      } else {
        // LocalCellId comes from the existing environment/initialization
        // certificate. Never infer a cell or declaration from a register name.
        auto expression = std::move(assignment->value);
        head.operation = SourceLocalAssignment{local->cell, std::move(expression)};
      }
      tail.operation = analysis::NoOperation{};
      tail.previous_origin = head.pc;
      ++i;
    }
  }
}
}  // namespace abcd::source
