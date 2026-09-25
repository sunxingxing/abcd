#include "class_assembly.h"

#include <algorithm>

namespace abcd::source {
namespace {
struct ClassHome {
  analysis::InstructionId creation;
  abc::ClassPlacement placement;
};
struct MethodHome {
  ClassHome home;
  analysis::InstructionId creation;
};
using AssemblyValue = std::variant<std::monostate, ClassHome, MethodHome,
                                   abc::PoolIndex, analysis::SpecialValue>;
bool SameHome(ClassHome left, ClassHome right) {
  return left.creation == right.creation && left.placement == right.placement;
}
bool Special(const AssemblyValue& value, analysis::SpecialValue wanted) {
  const auto* actual = std::get_if<analysis::SpecialValue>(&value);
  return actual && *actual == wanted;
}
}  // namespace

std::optional<SourceFailure> FoldClassAssembly(
    const AstMethod& method, SourceAst& ast, std::size_t bytes) {
  if (method.ssa.IsPhased()) return {};
  if (std::none_of(method.instructions.begin(), method.instructions.end(),
          [](const auto& instruction) {
            return std::holds_alternative<analysis::ClassCreation>(instruction.operation);
          })) return {};
  const auto instructions = method.instructions.size();
  const auto values = method.ssa.ValueCount();
  constexpr auto kValueBytes = sizeof(AssemblyValue) + sizeof(std::size_t);
  constexpr auto kInstructionBytes = sizeof(analysis::InstructionId) + sizeof(std::uint8_t);
  if (values > bytes / kValueBytes) return SourceFailure{SourceError::kResourceLimit, {}};
  bytes -= values * kValueBytes;
  if (instructions > bytes / kInstructionBytes)
    return SourceFailure{SourceError::kResourceLimit, {}};
  bytes -= instructions * kInstructionBytes;
  std::vector<AssemblyValue> facts(values);
  std::vector<std::size_t> last_use(values);
  std::vector<std::uint8_t> installed(instructions);
  std::vector<analysis::InstructionId> removed;
  removed.reserve(instructions);
  const auto extra = (facts.capacity() - values) * sizeof(AssemblyValue) +
      (last_use.capacity() - values) * sizeof(std::size_t) +
      (installed.capacity() - instructions) * sizeof(std::uint8_t) +
      (removed.capacity() - instructions) * sizeof(analysis::InstructionId);
  if (extra > bytes) return SourceFailure{SourceError::kResourceLimit, {}};
  bytes -= extra;
  for (std::size_t i = 0; i < instructions; ++i)
    for (const auto& read : method.ssa.Reads({static_cast<std::uint32_t>(i)}))
      last_use[read.value.value] = i;
  for (std::size_t v = 0; v < values; ++v)
    for (const auto& input : method.ssa.Inputs({static_cast<std::uint32_t>(v)}))
      last_use[input.value.value] = instructions;
  const auto input = [&](analysis::InstructionId i, analysis::ScalarSlot slot) -> AssemblyValue {
    for (const auto& read : method.ssa.Reads(i)) {
      const auto actual = method.ssa.Slot(read.value);
      if (!actual || actual->index() != slot.index()) continue;
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot);
          reg && *reg != std::get<abc::RegisterId>(*actual)) continue;
      return facts[read.value.value];
    }
    return {};
  };
  for (const auto& block : method.flow.blocks) {
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    for (std::size_t begin = block.first_instruction; begin < end;) {
      const auto* creation = std::get_if<analysis::ClassCreation>(
          &method.instructions[begin].operation);
      if (!creation) { ++begin; continue; }
      const analysis::InstructionId owner{static_cast<std::uint32_t>(begin)};
      const auto result = method.ssa.Write(owner);
      if (!result) return SourceFailure{SourceError::kInvalidEffect, method.instructions[begin].pc};
      facts[result->value] = ClassHome{owner, abc::ClassPlacement::kStatic};
      removed.clear();
      std::vector<SourceClassMember> members;
      std::optional<abc::PoolIndex> prototype;
      std::optional<analysis::FunctionCreation> initializer;
      bool ordinary = false;
      bool valid = true;
      auto cursor = begin + 1;
      for (; cursor < end; ++cursor) {
        const analysis::InstructionId id{static_cast<std::uint32_t>(cursor)};
        const auto& operation = method.instructions[cursor].operation;
        AssemblyValue value;
        std::optional<SourceClassMember> member;
        if (const auto* scalar = std::get_if<analysis::ScalarAssignment>(&operation)) {
          const auto* atom = std::get_if<analysis::ScalarValue>(&scalar->expression);
          if (!atom || scalar->completion != analysis::CompletionEffect::kNormal ||
              std::holds_alternative<analysis::EmptyArray>(*atom)) break;
          if (const auto* reg = std::get_if<abc::RegisterId>(atom)) value = input(id, *reg);
          else if (std::holds_alternative<analysis::Accumulator>(*atom))
            value = input(id, analysis::Accumulator{});
          else if (const auto* special = std::get_if<analysis::SpecialValue>(atom)) value = *special;
          if (const auto* function = std::get_if<MethodHome>(&value)) {
            if (function->home.creation != owner) break;
            removed.push_back(id);
          }
        } else if (const auto* string = std::get_if<analysis::StringLoad>(&operation)) {
          value = string->value;
        } else if (const auto* read = std::get_if<analysis::NamedRead>(&operation)) {
          const auto receiver = input(id, analysis::Accumulator{});
          const auto* home = std::get_if<ClassHome>(&receiver);
          if (!home || home->creation != owner || home->placement != abc::ClassPlacement::kStatic ||
              (prototype && prototype->value != read->name.value)) break;
          prototype = read->name;
          value = ClassHome{owner, abc::ClassPlacement::kPrototype};
        } else if (const auto* function = std::get_if<analysis::FunctionCreation>(&operation)) {
          const auto receiver = input(id, analysis::Accumulator{});
          const auto* home = std::get_if<ClassHome>(&receiver);
          if (function->kind != analysis::CreationKind::kMethod ||
              function->home_object != analysis::HomeObjectInput::kAccumulator ||
              !home || home->creation != owner) break;
          value = MethodHome{*home, id};
          removed.push_back(id);
        } else if (const auto* accessor = std::get_if<analysis::DefineAccessor>(&operation)) {
          const auto receiver = input(id, accessor->object);
          const auto key = input(id, accessor->key);
          const auto getter = input(id, accessor->getter);
          const auto setter = input(id, accessor->setter);
          const auto* home = std::get_if<ClassHome>(&receiver);
          const auto* name = std::get_if<abc::PoolIndex>(&key);
          const auto* get = std::get_if<MethodHome>(&getter);
          const auto* set = std::get_if<MethodHome>(&setter);
          const bool has_get = get && Special(setter, analysis::SpecialValue::kUndefined);
          const bool has_set = set && Special(getter, analysis::SpecialValue::kUndefined);
          if (ordinary || !home || home->creation != owner || !name || has_get == has_set ||
              !Special(input(id, analysis::Accumulator{}), analysis::SpecialValue::kFalse)) break;
          const auto& method_home = has_get ? *get : *set;
          if (!SameHome(*home, method_home.home) || installed[method_home.creation.value]) break;
          const auto& function = std::get<analysis::FunctionCreation>(
              method.instructions[method_home.creation.value].operation);
          if (function.length.value != (has_get ? 0 : 1)) break;
          installed[method_home.creation.value] = true;
          member = SourceClassMember{function, *name, home->placement,
              has_get ? abc::ClassMethodKind::kGetter : abc::ClassMethodKind::kSetter};
          value = *home;
        } else if (const auto* store = std::get_if<SourceMethodStore>(&ast.statements[cursor].operation)) {
          const auto receiver = input(id, store->write.object);
          const auto function_value = input(id, analysis::Accumulator{});
          const auto* home = std::get_if<ClassHome>(&receiver);
          const auto* function = std::get_if<MethodHome>(&function_value);
          if (!home || home->creation != owner || !function ||
              !SameHome(*home, function->home) || installed[function->creation.value]) break;
          installed[function->creation.value] = true;
          member = SourceClassMember{
              std::get<analysis::FunctionCreation>(method.instructions[function->creation.value].operation),
              store->write.name, home->placement, abc::ClassMethodKind::kOrdinary};
          ordinary = true;
        } else if (const auto* call = std::get_if<SourceInitializerCall>(
                       &ast.statements[cursor].operation)) {
          const auto receiver = input(id, call->call.receiver);
          const auto callee = input(id, analysis::Accumulator{});
          const auto* home = std::get_if<ClassHome>(&receiver);
          const auto* function = std::get_if<MethodHome>(&callee);
          if (!members.empty() || initializer || call->call.Count() != 0 ||
              !home || home->creation != owner ||
              home->placement != abc::ClassPlacement::kStatic || !function ||
              !SameHome(*home, function->home) || installed[function->creation.value]) break;
          const auto& creation = std::get<analysis::FunctionCreation>(
              method.instructions[function->creation.value].operation);
          if (creation.length.value != 0) break;
          installed[function->creation.value] = true;
          initializer = creation;
          // Only pure unobserved class assembly precedes this effect. Stop
          // here: later effects/members must not move ahead of initialization.
          ++cursor;
          break;
        } else break;
        if (const auto output = method.ssa.Write(id)) facts[output->value] = value;
        if (member) {
          if (members.size() == members.capacity()) {
            const auto old = members.capacity();
            const auto next = old ? old * 2 : 1;
            if (next < old || old > bytes / sizeof(SourceClassMember) ||
                next > bytes / sizeof(SourceClassMember) - old)
              return SourceFailure{SourceError::kResourceLimit, method.instructions[cursor].pc};
            members.reserve(next);
            if (members.capacity() > bytes / sizeof(SourceClassMember) - old)
              return SourceFailure{SourceError::kResourceLimit, method.instructions[cursor].pc};
          }
          members.push_back(*member);
        }
      }
      // Every eliminated method value, including each SSA copy, must be used
      // only inside this interval and installed once on its exact HomeObject.
      // Phi uses reject folding; no per-variable graph walks or alias guesses.
      for (const auto id : removed) {
        const auto output = method.ssa.Write(id);
        const auto* function = output ? std::get_if<MethodHome>(&facts[output->value]) : nullptr;
        if (!output || !function || !installed[function->creation.value] ||
            last_use[output->value] >= cursor) { valid = false; break; }
      }
      if (valid && (!members.empty() || initializer)) {
        auto* assignment = std::get_if<SourceAssignment>(&ast.statements[begin].operation);
        auto* expression = assignment ? std::get_if<ClassExpression>(&assignment->value) : nullptr;
        if (!expression) return SourceFailure{SourceError::kInvalidEffect, method.instructions[begin].pc};
        const auto retained = members.capacity() * sizeof(SourceClassMember);
        bytes -= retained;
        ast.allocated_bytes += retained;
        expression->prototype_key = prototype;
        expression->assembled_members = std::move(members);
        expression->initializer = initializer;
        for (const auto id : removed) {
          ast.statements[id.value].operation = analysis::NoOperation{};
          ast.statements[id.value].class_origin = owner;
        }
        for (auto i = begin + 1; i < cursor; ++i) {
          auto& statement = ast.statements[i];
          if (const auto* accessor = std::get_if<analysis::DefineAccessor>(&method.instructions[i].operation)) {
            statement.operation = SourceAssignment{analysis::Accumulator{},
                analysis::ScalarExpression{analysis::ScalarValue{accessor->object}}};
          } else if (std::holds_alternative<SourceMethodStore>(statement.operation)) {
            statement.operation = analysis::NoOperation{};
          } else if (std::holds_alternative<SourceInitializerCall>(statement.operation)) {
            // RecoverStaticFields must independently prove return undefined.
            statement.operation = SourceAssignment{analysis::Accumulator{},
                analysis::ScalarExpression{analysis::ScalarValue{analysis::SpecialValue::kUndefined}}};
          } else continue;
          statement.class_origin = owner;
        }
      }
      // Stop at the first barrier, then consider it as a possible new class.
      // Failed candidates never cause repeated scans of a suffix.
      begin = cursor;
    }
  }
  return {};
}
}  // namespace abcd::source
