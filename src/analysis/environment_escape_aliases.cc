#include <algorithm>
#include <limits>

#include "abcd/analysis/function_creation.h"
#include "environment_escape_builder.h"
#include "register_value_rule.h"

namespace abcd::analysis {
bool EnvironmentEscape::Builder::CellLess(const LexicalBinding& a,
                                          const LexicalBinding& b) {
  return a.environment != b.environment ? a.environment < b.environment
                                        : a.slot.value < b.slot.value;
}
EnvironmentEscape::Builder::AliasId EnvironmentEscape::Builder::Value(
    SsaValueId id) const {
  if (id.value >= ssa.Values().size()) throw EscapeError::kInvalidInput;
  return {id.value};
}
EnvironmentEscape::Builder::AliasId EnvironmentEscape::Builder::Environment(
    EnvironmentRef ref) const {
  if (const auto* local = std::get_if<EnvironmentId>(&ref)) {
    if (local->value >= flow.environments.Size())
      throw EscapeError::kInvalidInput;
    return {environment_begin + local->value};
  }
  return {captured};
}
EnvironmentEscape::Builder::AliasId EnvironmentEscape::Builder::Cell(
    const LexicalBinding& binding) {
  const auto found = std::lower_bound(cells.begin(), cells.end(), binding,
                                      [&](const auto& a, const auto& b) {
                                        Charge();
                                        return CellLess(a, b);
                                      });
  if (found == cells.end() || CellLess(binding, *found))
    throw EscapeError::kInvalidInput;
  return {cell_begin + static_cast<std::size_t>(found - cells.begin())};
}
EnvironmentEscape::Builder::AliasId EnvironmentEscape::Builder::Write(
    InstructionId id) const {
  const auto write = ssa.Write(id);
  if (!write) throw EscapeError::kInvalidInput;
  return Value(*write);
}
EnvironmentEscape::Builder::AliasId EnvironmentEscape::Builder::Read(
    InstructionId id) const {
  const auto reads = ssa.Reads(id);
  if (reads.size() != 1) throw EscapeError::kInvalidInput;
  return Value(reads.front().value);
}
void EnvironmentEscape::Builder::Extract() {
  if (body.instructions.size() > UINT32_MAX ||
      flow.before.size() != body.instructions.size() || !body.tries.empty() ||
      !body.handlers.empty() ||
      flow.allocations.size() != flow.environments.Size())
    throw EscapeError::kInvalidInput;
  for (const auto& site : flow.bindings) {
    Charge();
    cells.push_back(site.binding);
  }
  std::sort(cells.begin(), cells.end(), [&](const auto& a, const auto& b) {
    Charge();
    return CellLess(a, b);
  });
  cells.erase(std::unique(cells.begin(), cells.end(),
                          [&](const auto& a, const auto& b) {
                            Charge();
                            return !CellLess(a, b) && !CellLess(b, a);
                          }),
              cells.end());
  const auto add = [](std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a) throw std::bad_alloc{};
    return a + b;
  };
  environment_begin = ssa.Values().size();
  captured = add(environment_begin, flow.environments.Size());
  cell_begin = add(captured, 1);
  const auto count = add(cell_begin, cells.size());
  Charge(count);
  parents.resize(count);
  ranks.resize(count);
  exposed.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    Charge();
    parents[i] = {i};
  }
  Expose({captured});
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    Charge();
    if (!std::holds_alternative<PhiValue>(ssa.Values()[i].origin)) continue;
    for (const auto& input : ssa.Inputs({static_cast<std::uint32_t>(i)}))
      Unite({i}, Value(input.value));
  }
  for (std::size_t i = 0; i < flow.environments.Size(); ++i) {
    Charge();
    const EnvironmentId id{static_cast<std::uint32_t>(i)};
    const auto* definition = flow.environments.Find(id);
    if (!definition) throw EscapeError::kInvalidInput;
    edges.push_back({Environment(id), Environment(definition->parent)});
  }
  for (std::size_t i = 0; i < cells.size(); ++i) {
    Charge();
    const AliasId cell{cell_begin + i};
    const auto& binding = cells[i];
    if (const auto* local = std::get_if<EnvironmentId>(&binding.environment)) {
      const auto* definition = flow.environments.Find(*local);
      if (!definition || binding.slot.value >= definition->allocation.slots)
        throw EscapeError::kInvalidInput;
      edges.push_back({Environment(*local), cell});
    } else {
      Expose(cell);
    }
  }
  const auto lexical = [&](std::uint32_t instruction) {
    if (instruction >= body.instructions.size() || !flow.before[instruction])
      throw EscapeError::kInvalidInput;
    return LiftLexicalInstruction(body.instructions[instruction].decoded,
                                  frame_size);
  };
  for (std::size_t i = 0; i < flow.allocations.size(); ++i) {
    Charge();
    const auto site = flow.allocations[i];
    if (site.environment.value != i ||
        !std::holds_alternative<PushEnvironment>(lexical(site.instruction)))
      throw EscapeError::kInvalidInput;
    Unite(Write({site.instruction}), Environment(site.environment));
  }
  for (const auto& site : flow.bindings) {
    Charge();
    const auto operation = lexical(site.instruction);
    const auto cell = Cell(site.binding);
    if (std::holds_alternative<LexicalLoad>(operation))
      Unite(Write({site.instruction}), cell);
    else if (std::holds_alternative<LexicalStore>(operation))
      Unite(Read({site.instruction}), cell);
    else
      throw EscapeError::kInvalidInput;
  }
  for (std::uint32_t i = 0; i < body.instructions.size(); ++i) {
    Charge();
    if (!flow.before[i]) continue;
    const auto& instruction = body.instructions[i].decoded;
    if (std::holds_alternative<ScalarError>(
            LiftRegisterEffect(instruction, frame_size)))
      throw EscapeError::kInstruction;
    const auto lex = LiftLexicalInstruction(instruction, frame_size);
    if (std::holds_alternative<CurrentEnvironment>(lex)) {
      Unite(Write({i}), Environment(*flow.before[i]));
      continue;
    }
    if (!std::holds_alternative<LexicalError>(lex)) continue;
    if (std::get<LexicalError>(lex) != LexicalError::kUnsupportedOpcode)
      throw EscapeError::kInstruction;
    const auto scalar = LiftScalarInstruction(instruction, frame_size);
    if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar)) {
      if (const auto* value =
              std::get_if<ScalarValue>(&assignment->expression)) {
        if (std::holds_alternative<Accumulator>(*value) ||
            std::holds_alternative<abc::RegisterId>(*value)) {
          Unite(Write({i}), Read({i}));
          continue;
        }
        if (const auto* number = std::get_if<Float64Bits>(value)) {
          constexpr std::uint64_t kExponent = UINT64_C(0x7ff0000000000000);
          constexpr std::uint64_t kFraction = UINT64_C(0x000fffffffffffff);
          if ((number->value & kExponent) == kExponent &&
              (number->value & kFraction))
            throw EscapeError::kOpaqueValue;
        }
      } else if (!std::holds_alternative<PropertyRead>(
                     assignment->expression)) {
        const auto rule =
            detail::DescribeRegisterValue(instruction, frame_size);
        if (!std::holds_alternative<detail::ValueRule>(rule) ||
            std::get<detail::ValueRule>(rule).kind !=
                detail::ValueRuleKind::kLanguageOperation)
          throw EscapeError::kOpaqueValue;
      }
    }
    // Opaque consumers may retain values. Creation exposes current env even
    // when its result is unused or the target is sendable: this is
    // conservative, not evidence that those runtime paths all capture ordinary
    // environments.
    if (std::holds_alternative<FunctionCreation>(
            LiftFunctionCreation(instruction)))
      Expose(Environment(*flow.before[i]));
    for (const auto& read : ssa.Reads({i})) {
      Charge();
      Expose(Value(read.value));
    }
  }
}
}  // namespace abcd::analysis
