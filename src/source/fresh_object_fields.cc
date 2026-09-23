#include "fresh_object_fields.h"

#include <new>
#include <stdexcept>

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
class FreshObjectFields {
 public:
  std::optional<analysis::OwnWrite> Step(
      const abc::DecodedInstruction& instruction, std::size_t frame_size) {
    const auto lifted = analysis::LiftRegisterEffect(instruction, frame_size);
    const auto* effect = std::get_if<analysis::RegisterEffect>(&lifted);
    if (!effect) {
      created_ = false;
      stored_.reset();
      active_.reset();
      return {};
    }
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
        return *write;
    }
    if (active_ &&
        (Reads(*effect, *active_) || Writes(*effect, *active_)))
      active_.reset();
    return {};
  }
  bool Active() const { return active_.has_value(); }

 private:
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
      count += NamedDefinition(instruction.decoded.opcode);
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
        plan.sites.push_back({i, first, field->name, field->object});
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
