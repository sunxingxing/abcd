#include "parameter_defaults.h"

#include <algorithm>
#include <iterator>
#include <type_traits>

namespace abcd::source {
namespace {
using namespace analysis;
// v7 frontend: load local, save comparison operand, load undefined, strict
// comparison, conditional skip. Negated integer arms have five instructions.
constexpr std::size_t kGuardInstructions = 5;
constexpr std::size_t kLiteralArmInstructions = 2;
constexpr std::size_t kNegatedArmInstructions = 5;
struct Candidate {
  ParameterDefault source;
  InstructionId copy;
  SsaValueId incoming;
};
const ScalarAssignment* Assignment(const SemanticMethod& method, std::size_t i) {
  return std::get_if<ScalarAssignment>(&method.instructions[i].operation);
}
bool Slot(const ScalarSlot& actual, const ScalarSlot& expected) {
  if (actual.index() != expected.index()) return false;
  const auto* reg = std::get_if<abc::RegisterId>(&actual);
  return !reg || *reg == std::get<abc::RegisterId>(expected);
}
bool Copy(const SemanticMethod& method, std::size_t i,
          ScalarSlot target, ScalarSlot input) {
  const auto* assignment = Assignment(method, i);
  if (!assignment || assignment->completion != CompletionEffect::kNormal ||
      !Slot(assignment->destination, target)) return false;
  const auto* value = std::get_if<ScalarValue>(&assignment->expression);
  if (!value) return false;
  if (const auto* reg = std::get_if<abc::RegisterId>(value)) return Slot(*reg, input);
  return std::holds_alternative<Accumulator>(*value) && Slot(Accumulator{}, input);
}
const ScalarValue* Loaded(const SemanticMethod& method, std::size_t i) {
  const auto* assignment = Assignment(method, i);
  return assignment && assignment->completion == CompletionEffect::kNormal &&
      std::holds_alternative<Accumulator>(assignment->destination)
      ? std::get_if<ScalarValue>(&assignment->expression) : nullptr;
}
bool Undefined(const ScalarValue* value) {
  const auto* special = value ? std::get_if<SpecialValue>(value) : nullptr;
  return special && *special == SpecialValue::kUndefined;
}
bool HasOwner(const SourceStatement& node) {
  return node.constructor_arguments || node.class_origin || node.literal_origin;
}
std::optional<Candidate> Match(const SemanticMethod& method,
    const abc::FrameLayout& frame, const RegionIf& region) {
  if (region.alternate || region.body.value != region.header.value + 1 ||
      region.join.value != region.body.value + 1 ||
      region.join.value >= method.flow.blocks.size()) return {};
  const auto& header = method.flow.blocks[region.header.value];
  const auto& arm = method.flow.blocks[region.body.value];
  const auto& join = method.flow.blocks[region.join.value];
  if (header.instruction_count < kGuardInstructions ||
      (arm.instruction_count != kLiteralArmInstructions &&
       arm.instruction_count != kNegatedArmInstructions)) return {};
  const auto branch = std::size_t(header.first_instruction) + header.instruction_count - 1;
  const auto first = branch + 1 - kGuardInstructions;
  const auto body = branch + 1;
  const auto end = body + arm.instruction_count;
  if (arm.first_instruction != body || join.first_instruction != end ||
      end >= method.instructions.size()) return {};
  const auto* local_value = Loaded(method, first);
  const auto* local = local_value ? std::get_if<abc::RegisterId>(local_value) : nullptr;
  const auto* save = Assignment(method, first + 1);
  const auto* scratch = save ? std::get_if<abc::RegisterId>(&save->destination) : nullptr;
  if (!local || !scratch || *local == *scratch ||
      !Copy(method, first + 1, *scratch, Accumulator{}) ||
      !Undefined(Loaded(method, first + 2))) return {};
  const auto* compare = Assignment(method, first + 3);
  const auto* binary = compare ? std::get_if<BinaryExpression>(&compare->expression) : nullptr;
  const auto* left = binary ? std::get_if<abc::RegisterId>(&binary->left) : nullptr;
  const auto* jump = std::get_if<BranchOperation>(&method.instructions[branch].operation);
  if (!compare || !std::holds_alternative<Accumulator>(compare->destination) ||
      !binary || !left || *left != *scratch ||
      !std::holds_alternative<Accumulator>(binary->right) || !jump ||
      !std::holds_alternative<Accumulator>(jump->predicate.input) ||
      jump->target != join.begin ||
      !((binary->op == BinaryOperator::kStrictNotEqual &&
         jump->predicate.op == UnaryOperator::kTrueOrNonzeroNumber) ||
        (binary->op == BinaryOperator::kStrictEqual &&
         jump->predicate.op == UnaryOperator::kFalseOrZero))) return {};
  const auto reads = method.ssa.Reads({static_cast<std::uint32_t>(first)});
  if (reads.size() != 1) return {};
  const auto& copied = method.ssa.Values()[reads.front().value.value];
  const auto* definition = std::get_if<InstructionValue>(&copied.origin);
  if (!definition || definition->instruction.value >= first) return {};
  const auto incoming = method.ssa.Reads(definition->instruction);
  if (incoming.size() != 1) return {};
  const auto& argument = method.ssa.Values()[incoming.front().value.value];
  const auto* slot = std::get_if<abc::RegisterId>(&argument.slot);
  if (!slot || !std::holds_alternative<MethodEntry>(argument.origin) ||
      slot->value < frame.first_parameter.value ||
      slot->value - frame.first_parameter.value >= frame.parameters ||
      local->value >= frame.locals || scratch->value >= frame.locals ||
      !Copy(method, definition->instruction.value, *local, *slot) ||
      !Copy(method, end - 1, *local, Accumulator{})) return {};
  ParameterDefault source{slot->value - frame.first_parameter.value,
      {static_cast<std::uint32_t>(first)}, {static_cast<std::uint32_t>(branch)},
      {static_cast<std::uint32_t>(end)}, DefaultUndefined{}};
  const auto* literal = Loaded(method, body);
  if (const auto* integer = literal ? std::get_if<IntegerLiteral>(literal) : nullptr) {
    source.value = DefaultInteger{integer->value};
  } else if (!Undefined(literal) || arm.instruction_count != kLiteralArmInstructions) {
    return {};
  }
  if (arm.instruction_count == kNegatedArmInstructions) {
    auto* integer = std::get_if<DefaultInteger>(&source.value);
    const auto* negate = Assignment(method, body + 3);
    const auto* unary = negate ? std::get_if<UnaryExpression>(&negate->expression) : nullptr;
    if (!integer || integer->value < 0 ||
        !Copy(method, body + 1, *scratch, Accumulator{}) ||
        !Copy(method, body + 2, Accumulator{}, *scratch) || !unary ||
        unary->op != UnaryOperator::kNegate ||
        !std::holds_alternative<Accumulator>(unary->input) ||
        !std::holds_alternative<Accumulator>(negate->destination)) return {};
    // Keep a sign bit, not C++ integer negation: -0 is observable in JS.
    integer->negated = true;
  }
  return Candidate{source, definition->instruction, incoming.front().value};
}
}  // namespace

std::optional<SourceFailure> RecoverParameterDefaults(
    const analysis::SemanticMethod& method, const abc::FrameLayout& frame,
    std::uint16_t length, SourceAst& ast, std::size_t bytes) {
  if (length >= frame.parameters || ast.regions.catches || ast.argument_region ||
      method.instructions.size() != ast.statements.size()) return {};
  const auto blocks = method.flow.blocks.size();
  if (blocks > bytes / sizeof(const RegionIf*))
    return SourceFailure{SourceError::kResourceLimit, {}};
  std::vector<const RegionIf*> regions(blocks);
  if (regions.capacity() > bytes / sizeof(const RegionIf*))
    return SourceFailure{SourceError::kResourceLimit, {}};
  bytes -= regions.capacity() * sizeof(const RegionIf*);
  const auto collect = [&](const auto& self, const RegionNode& node) -> void {
    std::visit([&](const auto& region) {
      using T = std::decay_t<decltype(region)>;
      if constexpr (std::is_same_v<T, RegionIf>) {
        if (region.header.value < regions.size()) regions[region.header.value] = &region;
      } else if constexpr (std::is_same_v<T, RegionSequence>) {
        for (const auto& child : region.children) self(self, child);
      } else if constexpr (std::is_same_v<T, RegionLabel>) {
        if (region.body) self(self, *region.body);
      }
    }, node.value);
  };
  collect(collect, ast.regions.root);
  constexpr auto kCandidateBytes = sizeof(Candidate) + sizeof(ParameterDefault);
  if (frame.parameters > bytes / kCandidateBytes)
    return SourceFailure{SourceError::kResourceLimit, {}};
  std::vector<Candidate> candidates;
  candidates.reserve(frame.parameters);
  if (candidates.capacity() > bytes / kCandidateBytes)
    return SourceFailure{SourceError::kResourceLimit, {}};
  std::size_t block = 0;
  while (block < blocks && regions[block]) {
    auto candidate = Match(method, frame, *regions[block]);
    if (!candidate || (candidates.empty() ? candidate->source.parameter != length
        : candidate->source.parameter <= candidates.back().source.parameter)) break;
    if (!candidates.empty() && candidate->source.first.value !=
        candidates.back().source.end.value) break;
    candidates.push_back(*candidate);
    block = regions[block]->join.value;
  }
  if (candidates.empty()) return {};
  const auto first = candidates.front().source.first.value;
  const auto end = candidates.back().source.end.value;
  if (ast.super_arguments && ast.super_arguments->first.value < end) return {};
  // Before initialization only ABI transport and lexical-environment setup
  // are allowed. Incoming/default-local observations are checked below.
  for (std::size_t i = 0; i < first; ++i) {
    const auto& op = method.instructions[i].operation;
    if (const auto* scalar = std::get_if<ScalarAssignment>(&op)) {
      const auto* value = std::get_if<ScalarValue>(&scalar->expression);
      if (!value || std::holds_alternative<EmptyArray>(*value) ||
          scalar->completion != CompletionEffect::kNormal) return {};
    } else if (!std::holds_alternative<PushEnvironment>(op) &&
               !std::holds_alternative<LexicalStore>(op) &&
               !std::holds_alternative<NoOperation>(op)) return {};
  }
  for (std::size_t b = block; b < blocks; ++b)
    for (std::size_t edge = 0; edge < method.flow.blocks[b].successor_count; ++edge)
      if (method.flow.blocks[b].successors[edge].target.value < block) return {};
  const auto owner = [&](InstructionId instruction) -> const Candidate* {
    const auto it = std::upper_bound(candidates.begin(), candidates.end(), instruction.value,
        [](auto index, const Candidate& candidate) { return index < candidate.source.first.value; });
    if (it == candidates.begin()) return nullptr;
    const auto& candidate = *std::prev(it);
    return instruction.value < candidate.source.end.value ? &candidate : nullptr;
  };
  const auto parameter = [&](const ScalarSlot& slot) -> const Candidate* {
    const auto* reg = std::get_if<abc::RegisterId>(&slot);
    if (!reg || reg->value < frame.first_parameter.value) return nullptr;
    const auto ordinal = reg->value - frame.first_parameter.value;
    const auto it = std::lower_bound(candidates.begin(), candidates.end(), ordinal,
        [](const Candidate& c, auto p) { return c.source.parameter < p; });
    return it != candidates.end() && it->source.parameter == ordinal ? &*it : nullptr;
  };
  for (const auto& read : method.ssa.AllReads()) {
    const auto& value = method.ssa.Values()[read.value.value];
    if (const auto* candidate = parameter(value.slot)) {
      if (read.instruction != candidate->copy || read.value != candidate->incoming) return {};
    }
    const auto* definition = std::get_if<InstructionValue>(&value.origin);
    if (!definition) continue;
    const auto* source = owner(definition->instruction);
    if (source && definition->instruction.value + 1 != source->source.end.value &&
        owner(read.instruction) != source) return {};
    // A copied parameter must not escape before its default test. The copy
    // itself is found through the exact SSA definition, not a register name.
    const auto copy_read = method.ssa.Reads(definition->instruction);
    if (copy_read.size() == 1) {
      const auto* candidate = parameter(method.ssa.Values()[copy_read.front().value.value].slot);
      if (candidate && definition->instruction == candidate->copy &&
          read.instruction.value < candidate->source.end.value &&
          read.instruction != candidate->source.first) return {};
    }
  }
  for (std::size_t v = 0; v < method.ssa.Values().size(); ++v) {
    const auto& value = method.ssa.Values()[v];
    if (parameter(value.slot) && !std::holds_alternative<MethodEntry>(value.origin)) return {};
    for (const auto& input : method.ssa.Inputs({static_cast<std::uint32_t>(v)})) {
      const auto& origin = method.ssa.Values()[input.value.value];
      if (parameter(origin.slot)) return {};
      const auto* definition = std::get_if<InstructionValue>(&origin.origin);
      if (!definition) continue;
      const auto* source = owner(definition->instruction);
      if (source && definition->instruction.value + 1 != source->source.end.value) return {};
    }
  }
  // Final AST can have retargeted writes or merged reads. Admit only local
  // scalar/no-op/branch forms, and reject provenance crossing this interval.
  for (std::size_t i = 0; i < ast.statements.size(); ++i) {
    const auto& node = ast.statements[i];
    const bool inside = i >= first && i < end;
    if (inside) {
      if (HasOwner(node)) return {};
      if (const auto* assignment = std::get_if<SourceAssignment>(&node.operation)) {
        if (assignment->object_field || assignment->result != AssignmentResult::kStored ||
            !std::holds_alternative<ScalarExpression>(assignment->value)) return {};
      } else if (!std::holds_alternative<NoOperation>(node.operation) &&
                 !std::holds_alternative<SourceConditionalBreak>(node.operation)) return {};
    }
    const auto crosses = [&](std::optional<abc::CodeOffset> pc) {
      if (!pc) return false;
      const bool origin_inside = *pc >= ast.statements[first].pc &&
                                 *pc < ast.statements[end].pc;
      return origin_inside != inside;
    };
    if (crosses(node.fused_read) || crosses(node.fused_spill) ||
        crosses(node.fused_reload) || crosses(node.previous_origin)) return {};
  }
  ast.parameter_defaults.reserve(candidates.size());
  const auto retained = ast.parameter_defaults.capacity() * sizeof(ParameterDefault);
  if (retained > bytes - candidates.capacity() * sizeof(Candidate))
    return SourceFailure{SourceError::kResourceLimit, {}};
  for (const auto& candidate : candidates) ast.parameter_defaults.push_back(candidate.source);
  ast.allocated_bytes += retained;
  for (auto i = first; i < end; ++i)
    ast.statements[i] = SourceStatement{ast.statements[i].pc, NoOperation{}};
  return {};
}
}  // namespace abcd::source
