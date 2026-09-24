#include "constructor_fields.h"

#include <cstdint>
#include <new>
#include <stdexcept>
#include <variant>

#include "abcd/abc/control_transfer.h"
#include "abcd/abc/register_access.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/scalar.h"

namespace abcd::source {
namespace {
bool CopyOpcode(abc::Opcode opcode) {
  switch (opcode) {
    case abc::Opcode::kMOV_V4_V4:
    case abc::Opcode::kMOV_V8_V8:
    case abc::Opcode::kMOV_V16_V16:
    case abc::Opcode::kLDA_V8:
    case abc::Opcode::kSTA_V8:
      return true;
    default:
      return false;
  }
}
}  // namespace

ConstructorThisWrites ProveConstructorThisWrites(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    std::size_t bytes) {
  ConstructorThisWrites result;
  if (!frame.this_value || frame.this_value->value >= frame.registers ||
      !body.tries.empty() || !body.handlers.empty() ||
      frame.registers > bytes / sizeof(std::uint8_t))
    return result;
  std::size_t stores = 0;
  for (const auto& site : body.instructions) {
    const auto transfer = abc::GetControlTransfer(site.decoded.opcode);
    if (!transfer || (*transfer != abc::ControlTransfer::kFallthrough &&
                      *transfer != abc::ControlTransfer::kReturn &&
                      *transfer != abc::ControlTransfer::kThrow) ||
        site.branch_target)
      return result;
    if (std::holds_alternative<analysis::NamedWrite>(
            analysis::LiftNamedWrite(site.decoded, frame.registers)))
      ++stores;
  }
  const auto scratch = static_cast<std::size_t>(frame.registers);
  if (stores > (bytes - scratch) / sizeof(abc::CodeOffset)) return result;
  try {
    std::vector<std::uint8_t> aliases(frame.registers);
    result.sites.reserve(stores);
    if (aliases.capacity() > bytes ||
        result.sites.capacity() >
            (bytes - aliases.capacity()) / sizeof(abc::CodeOffset))
      return {};
    aliases[frame.this_value->value] = 1;
    bool accumulator = false;
    for (const auto& site : body.instructions) {
      const auto& instruction = site.decoded;
      if (CopyOpcode(instruction.opcode)) {
        const auto lifted = analysis::LiftScalarInstruction(
            instruction, frame.registers);
        const auto* assignment =
            std::get_if<analysis::ScalarAssignment>(&lifted);
        const auto* value = assignment
                                ? std::get_if<analysis::ScalarValue>(
                                      &assignment->expression)
                                : nullptr;
        if (!value || assignment->completion !=
                          analysis::CompletionEffect::kNormal)
          break;
        bool origin = false;
        if (std::holds_alternative<analysis::Accumulator>(*value))
          origin = accumulator;
        else if (const auto* slot = std::get_if<abc::RegisterId>(value))
          origin = aliases[slot->value];
        else
          break;
        if (std::holds_alternative<analysis::Accumulator>(
                assignment->destination))
          accumulator = origin;
        else
          aliases[std::get<abc::RegisterId>(assignment->destination).value] =
              origin;
        continue;
      }
      const auto named =
          analysis::LiftNamedWrite(instruction, frame.registers);
      if (const auto* write = std::get_if<analysis::NamedWrite>(&named);
          write && aliases[write->object.value])
        result.sites.push_back(site.offset);
      const auto accesses =
          abc::GetRegisterAccesses(instruction, frame.registers);
      const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
      if (!known) break;
      bool writes_register = false;
      for (std::size_t i = 0; i < known->count; ++i)
        writes_register |= known->windows[i].access ==
                           abc::OperandAccess::kWrite;
      if (writes_register) break;
      accumulator = false;
    }
    if (result.sites.empty()) return {};
    return result;
  } catch (const std::bad_alloc&) {
    return {};
  } catch (const std::length_error&) {
    return {};
  }
}
}  // namespace abcd::source
