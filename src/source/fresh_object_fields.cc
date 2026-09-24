#include "fresh_object_fields.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "abcd/analysis/define_instruction.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/register_effect.h"

namespace abcd::source {
namespace {
bool NamedDefinition(abc::Opcode opcode) {
  switch (opcode) {
    case abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8:
    case abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8:
      return true;
    default:
      return false;
  }
}
bool IndexedDefinition(abc::Opcode opcode) {
  return opcode ==
         abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8;
}
bool ValueDefinition(abc::Opcode opcode) {
  return opcode ==
         abc::Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8;
}
struct FreshDefinition {
  std::variant<abc::PoolIndex, std::uint32_t> key;
  abc::RegisterId object;
};
class FreshObjectFields {
 public:
  explicit FreshObjectFields(std::size_t frame_size)
      : generations_(frame_size, kAbsentGeneration) {}
  std::size_t AllocatedBytes() const {
    return generations_.capacity() * sizeof(std::uint32_t);
  }
  bool Fresh(abc::RegisterId object) const {
    return object.value < generations_.size() &&
           generations_[object.value] == generation_ &&
           (!pending_ || *pending_ != object);
  }
  void Reset() {
    if (generation_ == UINT32_MAX) {
      std::fill(generations_.begin(), generations_.end(),
                kAbsentGeneration);
      generation_ = kInitialGeneration;
    } else {
      ++generation_;
    }
    created_ = false;
    pending_.reset();
    acc_string_.reset();
    key_string_.reset();
  }
  std::optional<FreshDefinition> Step(
      const abc::DecodedInstruction& instruction, std::size_t frame_size) {
    const auto lifted = analysis::LiftRegisterEffect(instruction, frame_size);
    const auto* effect = std::get_if<analysis::RegisterEffect>(&lifted);
    if (!effect) {
      Reset();
      return {};
    }
    TrackString(instruction, *effect);
    if (created_) {
      created_ = false;
      if (instruction.opcode == abc::Opcode::kSTA_V8 &&
          effect->read_count == 1 &&
          std::holds_alternative<analysis::Accumulator>(effect->reads[0]) &&
          effect->write &&
          std::holds_alternative<abc::RegisterId>(*effect->write)) {
        const auto reg = std::get<abc::RegisterId>(*effect->write);
        generations_[reg.value] = generation_;
        pending_ = reg;
        return {};
      }
    }
    ResolvePending(*effect);
    const auto object = analysis::LiftObjectCreation(instruction);
    if (std::holds_alternative<analysis::EmptyObjectCreation>(object) ||
        std::holds_alternative<analysis::BufferedObjectCreation>(object)) {
      Invalidate(*effect);
      created_ = true;
      return {};
    }
    if (NamedDefinition(instruction.opcode)) {
      const auto field = analysis::LiftOwnWrite(instruction, frame_size);
      if (const auto* write = std::get_if<analysis::OwnWrite>(&field);
          write && write->kind == analysis::OwnWriteKind::kDefineField &&
          Fresh(write->object)) {
        Invalidate(*effect, write->object);
        return FreshDefinition{write->name, write->object};
      }
    }
    if (IndexedDefinition(instruction.opcode)) {
      const auto field = analysis::LiftDefineInstruction(instruction,
                                                        frame_size);
      if (const auto* write =
              std::get_if<analysis::DefineFieldByIndex>(&field);
          write && Fresh(write->object)) {
        Invalidate(*effect, write->object);
        return FreshDefinition{write->index, write->object};
      }
    }
    if (ValueDefinition(instruction.opcode) && key_string_) {
      const auto field = analysis::LiftDefineInstruction(instruction,
                                                        frame_size);
      if (const auto* write =
              std::get_if<analysis::DefineFieldByValue>(&field);
          write && Fresh(write->object) &&
          write->key == key_string_->register_id) {
        Invalidate(*effect, write->object);
        return FreshDefinition{key_string_->pool, write->object};
      }
    }
    Invalidate(*effect);
    return {};
  }

 private:
  static constexpr std::uint32_t kAbsentGeneration = 0;
  static constexpr std::uint32_t kInitialGeneration = 1;
  struct StringBinding {
    abc::RegisterId register_id;
    abc::PoolIndex pool;
  };
  void ResolvePending(const analysis::RegisterEffect& effect) {
    if (!pending_) return;
    bool observes = false;
    for (std::size_t i = 0; i < effect.read_count; ++i)
      observes |= std::holds_alternative<analysis::Accumulator>(
                      effect.reads[i]) ||
                  (std::holds_alternative<abc::RegisterId>(effect.reads[i]) &&
                   std::get<abc::RegisterId>(effect.reads[i]) == *pending_);
    if (effect.range)
      observes |= pending_->value >= effect.range->first.value &&
                  pending_->value - effect.range->first.value <
                      effect.range->count;
    if (const auto* write = effect.write
                                ? std::get_if<abc::RegisterId>(&*effect.write)
                                : nullptr;
        write && *write == *pending_)
      observes = true;
    if (observes) {
      generations_[pending_->value] = kAbsentGeneration;
      pending_.reset();
    } else if (effect.write &&
               std::holds_alternative<analysis::Accumulator>(*effect.write)) {
      pending_.reset();
    }
  }
  void TrackString(const abc::DecodedInstruction& instruction,
                   const analysis::RegisterEffect& effect) {
    const auto* register_id = effect.write
                                  ? std::get_if<abc::RegisterId>(&*effect.write)
                                  : nullptr;
    if (register_id && key_string_ &&
        *register_id == key_string_->register_id)
      key_string_.reset();
    if (instruction.opcode == abc::Opcode::kSTA_V8 && register_id &&
        acc_string_)
      key_string_ = StringBinding{*register_id, *acc_string_};
    if (effect.write &&
        std::holds_alternative<analysis::Accumulator>(*effect.write)) {
      acc_string_.reset();
      if (instruction.opcode == abc::Opcode::kLDA_STR_ID16)
        acc_string_ = abc::PoolIndex{static_cast<std::uint32_t>(
            instruction.operands[0].raw_bits)};
    }
  }
  void Invalidate(const analysis::RegisterEffect& effect,
                  std::optional<abc::RegisterId> receiver = {}) {
    for (std::size_t i = 0; i < effect.read_count; ++i)
      if (const auto* read = std::get_if<abc::RegisterId>(&effect.reads[i]);
          read && (!receiver || *read != *receiver))
        generations_[read->value] = kAbsentGeneration;
    if (effect.range)
      for (std::uint32_t i = 0; i < effect.range->count; ++i) {
        const auto reg = effect.range->first.value + i;
        if (!receiver || reg != receiver->value)
          generations_[reg] = kAbsentGeneration;
      }
    const auto* write = effect.write
                                  ? std::get_if<abc::RegisterId>(&*effect.write)
                                  : nullptr;
    if (write) generations_[write->value] = kAbsentGeneration;
  }
  bool created_ = false;
  std::vector<std::uint32_t> generations_;
  std::uint32_t generation_ = kInitialGeneration;
  std::optional<abc::RegisterId> pending_;
  std::optional<abc::PoolIndex> acc_string_;
  std::optional<StringBinding> key_string_;
};
std::size_t CountDefinitions(
    std::span<const abc::BodyInstruction> instructions) {
  std::size_t count = 0;
  for (const auto& instruction : instructions)
    count += NamedDefinition(instruction.decoded.opcode) ||
             IndexedDefinition(instruction.decoded.opcode) ||
             ValueDefinition(instruction.decoded.opcode);
  return count;
}
void Collect(std::span<const abc::BodyInstruction> instructions,
             std::size_t base, std::size_t frame_size,
             FreshObjectFields& tracker, FreshFieldPlan& plan) {
  bool in_chain = false;
  std::size_t first = 0;
  abc::RegisterId previous{};
  for (std::size_t i = 0; i < instructions.size(); ++i) {
    const auto field = tracker.Step(instructions[i].decoded, frame_size);
    if (field) {
      if (!in_chain || previous != field->object)
        first = plan.sites.size();
      plan.sites.push_back({base + i, first, field->key, field->object});
      in_chain = true;
      previous = field->object;
    } else if (in_chain && !tracker.Fresh(previous)) {
      in_chain = false;
    }
  }
}
}  // namespace

std::variant<FreshFieldPlan, BodyEmitFailure> BuildFreshFieldPlan(
    std::span<const abc::BodyInstruction> instructions, std::size_t frame_size,
    std::size_t budget_bytes) {
  const auto resource = [](abc::CodeOffset offset) {
    return BodyEmitFailure{BodyEmitError::kResourceLimit, offset, {}, {}};
  };
  try {
    const auto count = CountDefinitions(instructions);
    if (count > budget_bytes / sizeof(FreshFieldSite) || count > UINT32_MAX)
      return resource({});
    FreshFieldPlan plan;
    if (count == 0) return plan;
    plan.sites.reserve(count);
    if (plan.AllocatedBytes() > budget_bytes) return resource({});
    const auto remaining = budget_bytes - plan.AllocatedBytes();
    if (frame_size > remaining / sizeof(std::uint32_t))
      return resource({});
    FreshObjectFields tracker(frame_size);
    if (tracker.AllocatedBytes() > remaining) return resource({});
    Collect(instructions, 0, frame_size, tracker, plan);
    return plan;
  } catch (const std::bad_alloc&) {
    return resource({});
  } catch (const std::length_error&) {
    return resource({});
  }
}
std::variant<FreshFieldPlan, BodyEmitFailure> BuildBranchFreshFieldPlan(
    const abc::MethodBody& body, const analysis::NormalFlow& graph,
    std::size_t frame_size, std::size_t budget_bytes) {
  const auto resource = [] {
    return BodyEmitFailure{BodyEmitError::kResourceLimit, {}, {}, {}};
  };
  try {
    const auto count = CountDefinitions(body.instructions);
    if (count > budget_bytes / sizeof(FreshFieldSite) || count > UINT32_MAX)
      return resource();
    FreshFieldPlan plan;
    if (count == 0) return plan;
    plan.sites.reserve(count);
    if (plan.AllocatedBytes() > budget_bytes) return resource();
    const auto remaining = budget_bytes - plan.AllocatedBytes();
    if (frame_size > remaining / sizeof(std::uint32_t))
      return resource();
    FreshObjectFields tracker(frame_size);
    if (tracker.AllocatedBytes() > remaining) return resource();
    for (const auto& block : graph.blocks) {
      if (block.first_instruction > body.instructions.size() ||
          block.instruction_count >
              body.instructions.size() - block.first_instruction)
        return BodyEmitFailure{BodyEmitError::kUnsupportedInstruction,
                               block.begin, {}, {}};
      tracker.Reset();
      Collect(std::span(body.instructions).subspan(
                  block.first_instruction, block.instruction_count),
              block.first_instruction, frame_size, tracker, plan);
    }
    return plan;
  } catch (const std::bad_alloc&) {
    return resource();
  } catch (const std::length_error&) {
    return resource();
  }
}
std::variant<FreshFieldPlan, BodyEmitFailure> BuildFreshFieldPlan(
    const abc::MethodBody& body, std::size_t frame_size,
    std::size_t budget_bytes) {
  return BuildFreshFieldPlan(std::span(body.instructions), frame_size,
                             budget_bytes);
}
}  // namespace abcd::source
