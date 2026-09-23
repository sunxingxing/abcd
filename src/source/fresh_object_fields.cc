#include "fresh_object_fields.h"

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
  std::optional<FreshDefinition> Step(
      const abc::DecodedInstruction& instruction, std::size_t frame_size) {
    const auto lifted = analysis::LiftRegisterEffect(instruction, frame_size);
    const auto* effect = std::get_if<analysis::RegisterEffect>(&lifted);
    if (!effect) {
      created_ = false;
      stored_.reset();
      active_.reset();
      acc_string_.reset();
      key_string_.reset();
      return {};
    }
    TrackString(instruction, *effect);
    if (created_) {
      created_ = false;
      if (instruction.opcode == abc::Opcode::kSTA_V8 &&
          effect->read_count == 1 &&
          std::holds_alternative<analysis::Accumulator>(effect->reads[0]) &&
          effect->write &&
          std::holds_alternative<abc::RegisterId>(*effect->write))
        stored_ = std::get<abc::RegisterId>(*effect->write);
      return {};
    }
    if (stored_) {
      const bool overwrites_acc =
          effect->write &&
          std::holds_alternative<analysis::Accumulator>(*effect->write);
      active_ = overwrites_acc && !Reads(*effect, *stored_) &&
                        !ReadsAccumulator(*effect)
                    ? stored_
                    : std::nullopt;
      stored_.reset();
    }
    const auto object = analysis::LiftObjectCreation(instruction);
    if (std::holds_alternative<analysis::EmptyObjectCreation>(object) ||
        std::holds_alternative<analysis::BufferedObjectCreation>(object)) {
      active_.reset();
      created_ = true;
      return {};
    }
    if (NamedDefinition(instruction.opcode) && active_) {
      const auto field = analysis::LiftOwnWrite(instruction, frame_size);
      if (const auto* write = std::get_if<analysis::OwnWrite>(&field);
          write && write->kind == analysis::OwnWriteKind::kDefineField &&
          write->object == *active_)
        return FreshDefinition{write->name, write->object};
    }
    if (IndexedDefinition(instruction.opcode) && active_) {
      const auto field = analysis::LiftDefineInstruction(instruction,
                                                        frame_size);
      if (const auto* write =
              std::get_if<analysis::DefineFieldByIndex>(&field);
          write && write->object == *active_)
        return FreshDefinition{write->index, write->object};
    }
    if (ValueDefinition(instruction.opcode) && active_ && key_string_) {
      const auto field = analysis::LiftDefineInstruction(instruction,
                                                        frame_size);
      if (const auto* write =
              std::get_if<analysis::DefineFieldByValue>(&field);
          write && write->object == *active_ &&
          write->key == key_string_->register_id)
        return FreshDefinition{key_string_->pool, write->object};
    }
    if (active_ &&
        (Reads(*effect, *active_) || Writes(*effect, *active_)))
      active_.reset();
    return {};
  }
  bool Active() const { return active_.has_value(); }

 private:
  struct StringBinding {
    abc::RegisterId register_id;
    abc::PoolIndex pool;
  };
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
  static bool Reads(const analysis::RegisterEffect& effect,
                    abc::RegisterId reg) {
    for (std::size_t i = 0; i < effect.read_count; ++i)
      if (const auto* read = std::get_if<abc::RegisterId>(&effect.reads[i]);
          read && *read == reg)
        return true;
    return effect.range && reg.value >= effect.range->first.value &&
           reg.value - effect.range->first.value < effect.range->count;
  }
  static bool ReadsAccumulator(const analysis::RegisterEffect& effect) {
    for (std::size_t i = 0; i < effect.read_count; ++i)
      if (std::holds_alternative<analysis::Accumulator>(effect.reads[i]))
        return true;
    return false;
  }
  static bool Writes(const analysis::RegisterEffect& effect,
                     abc::RegisterId reg) {
    const auto* write = effect.write
                            ? std::get_if<abc::RegisterId>(&*effect.write)
                            : nullptr;
    return write && *write == reg;
  }
  bool created_ = false;
  std::optional<abc::RegisterId> stored_;
  std::optional<abc::RegisterId> active_;
  std::optional<abc::PoolIndex> acc_string_;
  std::optional<StringBinding> key_string_;
};
}  // namespace

std::variant<FreshFieldPlan, BodyEmitFailure> BuildFreshFieldPlan(
    const abc::MethodBody& body, std::size_t frame_size,
    std::size_t budget_bytes) {
  const auto resource = [](abc::CodeOffset offset) {
    return BodyEmitFailure{BodyEmitError::kResourceLimit, offset, {}, {}};
  };
  try {
    std::size_t count = 0;
    for (const auto& instruction : body.instructions)
      count += NamedDefinition(instruction.decoded.opcode) ||
               IndexedDefinition(instruction.decoded.opcode) ||
               ValueDefinition(instruction.decoded.opcode);
    if (count > budget_bytes / sizeof(FreshFieldSite) || count > UINT32_MAX)
      return resource({});
    FreshFieldPlan plan;
    plan.sites.reserve(count);
    if (plan.AllocatedBytes() > budget_bytes) return resource({});
    FreshObjectFields tracker;
    bool in_chain = false;
    std::size_t first = 0;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto& instruction = body.instructions[i];
      const auto field = tracker.Step(instruction.decoded, frame_size);
      if (field) {
        if (!in_chain) first = plan.sites.size();
        plan.sites.push_back({i, first, field->key, field->object});
        in_chain = true;
      } else if (!tracker.Active()) {
        in_chain = false;
      }
    }
    return plan;
  } catch (const std::bad_alloc&) {
    return resource({});
  } catch (const std::length_error&) {
    return resource({});
  }
}
}  // namespace abcd::source
