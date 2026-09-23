#include "parameter_names.h"

#include <algorithm>
#include <new>
#include <optional>
#include <stdexcept>

#include "abcd/analysis/scalar.h"

namespace abcd::source {
namespace {
struct PrefixMove {
  std::uint32_t destination;
  std::uint32_t source;
  std::size_t ordinal;
};
}  // namespace
ParameterOffsetResult MapParameterDebugNames(
    const abc::FrameLayout& frame, const abc::MethodBody& body,
    const abc::DebugScopes& scopes, std::size_t bytes) {
  using Error = ParameterNameError;
  try {
    if (body.instructions.empty() || frame.parameters == 0)
      return Error::kUnproven;
    std::size_t prefix_count = 0;
    using abc::Opcode;
    for (const auto& instruction : body.instructions) {
      if (instruction.decoded.opcode != Opcode::kMOV_V4_V4 &&
          instruction.decoded.opcode != Opcode::kMOV_V8_V8)
        break;
      ++prefix_count;
    }
    if (prefix_count == 0 || prefix_count == body.instructions.size())
      return Error::kUnproven;
    const auto start = body.instructions[prefix_count].offset.value;
    for (const auto& instruction : body.instructions)
      if (instruction.branch_target &&
          instruction.branch_target->value < start)
        return Error::kUnproven;
    if (prefix_count > bytes / sizeof(PrefixMove))
      return Error::kResourceLimit;
    bytes -= prefix_count * sizeof(PrefixMove);
    std::vector<PrefixMove> moves;
    moves.reserve(prefix_count);
    if (moves.capacity() > prefix_count &&
        moves.capacity() - prefix_count > bytes / sizeof(PrefixMove))
      return Error::kResourceLimit;
    bytes -= (moves.capacity() - prefix_count) * sizeof(PrefixMove);
    for (std::size_t i = 0; i < prefix_count; ++i) {
      const auto lifted = analysis::LiftScalarInstruction(
          body.instructions[i].decoded, frame.registers);
      const auto* assignment = std::get_if<analysis::ScalarAssignment>(&lifted);
      if (!assignment) return Error::kUnproven;
      const auto* destination =
          std::get_if<abc::RegisterId>(&assignment->destination);
      const auto* expression =
          std::get_if<analysis::ScalarValue>(&assignment->expression);
      const auto* source = expression
                               ? std::get_if<abc::RegisterId>(expression)
                               : nullptr;
      if (!destination || !source) return Error::kUnproven;
      // A copied formal is no longer the entry value if its ABI slot itself
      // was overwritten earlier in the prologue.
      if (destination->value >= frame.first_parameter.value &&
          destination->value - frame.first_parameter.value <
              frame.parameters)
        return Error::kUnproven;
      moves.push_back({destination->value, source->value, i});
    }
    std::sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) {
      return a.destination != b.destination
                 ? a.destination < b.destination
                 : a.ordinal < b.ordinal;
    });
    if (frame.parameters > bytes / sizeof(std::optional<abc::EntityOffset>))
      return Error::kResourceLimit;
    bytes -= frame.parameters * sizeof(std::optional<abc::EntityOffset>);
    std::vector<std::optional<abc::EntityOffset>> names(frame.parameters);
    if (names.capacity() > frame.parameters &&
        names.capacity() - frame.parameters >
            bytes / sizeof(std::optional<abc::EntityOffset>))
      return Error::kResourceLimit;
    bytes -= (names.capacity() - frame.parameters) *
             sizeof(std::optional<abc::EntityOffset>);
    for (const auto& local : scopes.locals) {
      if (local.start != start || local.reg < 0 ||
          static_cast<std::uint32_t>(local.reg) >= frame.locals)
        continue;
      const auto dest = static_cast<std::uint32_t>(local.reg);
      const auto found = std::upper_bound(
          moves.begin(), moves.end(), dest,
          [](std::uint32_t reg, const PrefixMove& move) {
            return reg < move.destination;
          });
      if (found == moves.begin() || (found - 1)->destination != dest)
        continue;
      const auto source = (found - 1)->source;
      if (source < frame.first_parameter.value ||
          source - frame.first_parameter.value >= frame.parameters)
        continue;
      auto& name = names[source - frame.first_parameter.value];
      if (name) return Error::kUnproven;
      name = local.name;
    }
    if (std::any_of(names.begin(), names.end(),
                    [](const auto& name) { return !name; }))
      return Error::kUnproven;
    if (names.size() > bytes / sizeof(abc::EntityOffset))
      return Error::kResourceLimit;
    ParameterOffsets result;
    result.reserve(names.size());
    if (result.capacity() > names.size() &&
        result.capacity() - names.size() > bytes / sizeof(abc::EntityOffset))
      return Error::kResourceLimit;
    for (const auto& name : names) result.push_back(*name);
    return result;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
