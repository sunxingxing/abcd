#include "abcd/analysis/value_ir.h"

#include <new>
#include <stdexcept>

namespace abcd::analysis {
std::optional<ValueGraphError> VerifyValueUses(
    const ValueProgram& program, std::size_t scratch_bytes) {
  if (program.uses.size() > scratch_bytes)
    return ValueGraphError::kResourceLimit;
  if (!program.phases.empty() && program.phases.size() != program.blocks.size())
    return ValueGraphError::kDefinition;
  try {
    std::vector<std::uint8_t> linked(program.uses.size());
    for (std::size_t index = 0; index < program.values.size(); ++index) {
      const auto& definition = program.values[index];
      if (definition.life == ValueLife::kRemoved && definition.first_use)
        return ValueGraphError::kDefinition;
      if (const auto* result = std::get_if<OpResult>(&definition.origin)) {
        if (program.phases.empty() != !result->commit)
          return ValueGraphError::kDefinition;
        if (result->commit && (result->commit->value >= program.phases.size() ||
            program.phases[result->commit->value].phase != ExceptionPhase::kCommit ||
            program.phases[result->commit->value].operation != result->op))
          return ValueGraphError::kDefinition;
        if (result->op.value >= program.ops.size() ||
            !program.ops[result->op.value].result ||
            program.ops[result->op.value].result->value != index ||
            program.ops[result->op.value].life != definition.life)
          return ValueGraphError::kDefinition;
      } else if (const auto* phi = std::get_if<PhiDefinition>(&definition.origin)) {
        if (phi->block.value >= program.blocks.size())
          return ValueGraphError::kDefinition;
      } else if (const auto* caught = std::get_if<CatchDefinition>(&definition.origin)) {
        if (definition.life != ValueLife::kLive ||
            caught->block.value >= program.phases.size() ||
            program.phases[caught->block.value].phase != ExceptionPhase::kCatch)
          return ValueGraphError::kDefinition;
      } else if (definition.life != ValueLife::kLive) {
        return ValueGraphError::kDefinition;
      }
      std::optional<UseId> previous;
      auto cursor = definition.first_use;
      while (cursor) {
        if (cursor->value >= program.uses.size() || linked[cursor->value])
          return ValueGraphError::kUseLink;
        linked[cursor->value] = 1;
        const auto& use = program.uses[cursor->value];
        const auto* value = std::get_if<ValueId>(&use.operand);
        if (use.life != ValueLife::kLive || !value ||
            value->value != index || use.previous != previous)
          return ValueGraphError::kUseLink;
        previous = cursor;
        cursor = use.next;
      }
    }
    for (std::size_t index = 0; index < program.ops.size(); ++index) {
      const auto& op = program.ops[index];
      if (op.first_use.value > program.uses.size() ||
          op.use_count > program.uses.size() - op.first_use.value)
        return ValueGraphError::kUseRange;
      if (op.result) {
        if (op.result->value >= program.values.size())
          return ValueGraphError::kDefinition;
        const auto* result = std::get_if<OpResult>(
            &program.values[op.result->value].origin);
        if (!result || result->op.value != index)
          return ValueGraphError::kDefinition;
      }
      // Each valid range owns disjoint occurrences; a wrong overlapping range
      // fails on its first mismatched owner rather than repeated global scans.
      for (std::size_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
        const auto& use = program.uses[op.first_use.value + ordinal];
        const auto* owner = std::get_if<OpId>(&use.owner);
        if (!owner || owner->value != index || use.incoming ||
            use.life != op.life)
          return ValueGraphError::kUseOwner;
      }
    }
    for (std::size_t index = 0; index < program.uses.size(); ++index) {
      const auto& use = program.uses[index];
      if (use.life == ValueLife::kRemoved) {
        if (use.previous || use.next || linked[index])
          return ValueGraphError::kUseLink;
        if (const auto* owner = std::get_if<OpId>(&use.owner)) {
          if (owner->value >= program.ops.size() ||
              program.ops[owner->value].life != ValueLife::kRemoved)
            return ValueGraphError::kUseOwner;
          const auto& op = program.ops[owner->value];
          if (index < op.first_use.value ||
              index - op.first_use.value >= op.use_count || use.incoming)
            return ValueGraphError::kUseRange;
        } else {
          const auto phi = std::get<ValueId>(use.owner);
          if (phi.value >= program.values.size() || !use.incoming || use.input ||
              use.role != OperandRole::kPhiInput ||
              !std::holds_alternative<ValueId>(use.operand) ||
              program.values[phi.value].life != ValueLife::kRemoved ||
              !std::holds_alternative<PhiDefinition>(program.values[phi.value].origin))
            return ValueGraphError::kUseOwner;
        }
        continue;
      }
      if (std::holds_alternative<ValueId>(use.operand)) {
        if (!linked[index]) return ValueGraphError::kUseLink;
        const auto value = std::get<ValueId>(use.operand);
        if (program.values[value.value].life != ValueLife::kLive)
          return ValueGraphError::kDefinition;
        const auto* result = std::get_if<OpResult>(
            &program.values[value.value].origin);
        if (result && program.ops[result->op.value].life == ValueLife::kRemoved)
          return ValueGraphError::kDefinition;
      } else if (use.previous || use.next || linked[index]) {
        return ValueGraphError::kUseLink;
      }
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        if (owner->value >= program.ops.size()) return ValueGraphError::kUseOwner;
        const auto& op = program.ops[owner->value];
        if (index < op.first_use.value ||
            index - op.first_use.value >= op.use_count || use.incoming)
          return ValueGraphError::kUseRange;
      } else {
        const auto phi = std::get<ValueId>(use.owner);
        if (phi.value >= program.values.size() || !use.incoming || use.input ||
            program.values[phi.value].life != ValueLife::kLive ||
            use.role != OperandRole::kPhiInput ||
            !std::holds_alternative<ValueId>(use.operand) ||
            !std::holds_alternative<PhiDefinition>(program.values[phi.value].origin))
          return ValueGraphError::kUseOwner;
      }
    }
    return {};
  } catch (const std::bad_alloc&) {
    return ValueGraphError::kResourceLimit;
  } catch (const std::length_error&) {
    return ValueGraphError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
