#include "accumulator_liveness.h"

#include <algorithm>
#include <type_traits>

namespace abcd::source {
namespace {
using Access = AccumulatorAccess;
bool Reads(const analysis::ScalarValue& value) {
  return std::holds_alternative<analysis::Accumulator>(value);
}
bool Reads(const MemberExpression& member) {
  return member.receiver || Reads(member.object);
}
bool Reads(const SourceExpression& expression) {
  return std::visit([](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarExpression>) {
      return analysis::ReadsAccumulator(value);
    } else if constexpr (std::is_same_v<T, MemberExpression>) {
      return Reads(value);
    } else if constexpr (std::is_same_v<T, StringComparison>) {
      return Reads(value.left);
    } else if constexpr (std::is_same_v<T, StringExpression> ||
                         std::is_same_v<T, GlobalExpression> ||
                         std::is_same_v<T, ModuleExpression> ||
                         std::is_same_v<T, CapturedExpression> ||
                         std::is_same_v<T, LocalExpression> ||
                         std::is_same_v<T, analysis::ConstructorCall> ||
                         std::is_same_v<T, analysis::EmptyObjectCreation> ||
                         std::is_same_v<T, analysis::BufferedObjectCreation> ||
                         std::is_same_v<T, analysis::BufferedArrayCreation> ||
                         std::is_same_v<T, analysis::GlobalObjectLoad>) {
      return false;
    } else {
      static_assert(std::is_same_v<T, CallExpression> ||
                    std::is_same_v<T, MemberCallExpression> ||
                    std::is_same_v<T, FunctionExpression> ||
                    std::is_same_v<T, ClassExpression> ||
                    std::is_same_v<T, MethodExpression> ||
                    std::is_same_v<T, AccessorExpression> ||
                    std::is_same_v<T, analysis::RestCopy> ||
                    std::is_same_v<T, analysis::AwaitValue> ||
                    std::is_same_v<T, analysis::SuperRange>);
      return true;
    }
  }, expression);
}
bool Reads(const FieldValue& stored) {
  return std::visit([](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, StringExpression>) return false;
    else return Reads(value);
  }, stored);
}
Access Read(bool value) { return value ? Access::kRead : Access::kNone; }
Access Classify(const SourceStatement& node) {
  if (node.constructor_arguments || node.class_origin || node.literal_origin) return Access::kRead;
  // Predicate origins relocate PC spans only. Predicate() reads the typed
  // condition/input below, not the erased acc-producing instructions.
  const bool predicate = std::holds_alternative<SourceConditionalBreak>(node.operation) ||
      std::holds_alternative<SourceConditionalContinue>(node.operation);
  if (!predicate && (node.fused_read || node.fused_spill || node.fused_reload)) return Access::kRead;
  return std::visit([](const auto& op) -> Access {
    using T = std::decay_t<decltype(op)>;
    if constexpr (std::is_same_v<T, SourceAssignment>) {
      if (op.object_field || op.result == AssignmentResult::kArgumentOnly || Reads(op.value))
        return Access::kRead;
      return op.result == AssignmentResult::kStored &&
          std::holds_alternative<analysis::Accumulator>(op.target)
          ? Access::kWrite : Access::kNone;
    } else if constexpr (std::is_same_v<T, SourceMemberStore>) {
      return Read(Reads(op.object) || Reads(op.value));
    } else if constexpr (std::is_same_v<T, SourceOwnStore>) {
      return Read(Reads(op.value));
    } else if constexpr (std::is_same_v<T, SourceReturn> || std::is_same_v<T, SourceThrow>) {
      return Read(Reads(op.value));
    } else if constexpr (std::is_same_v<T, SourceReturnExpression> ||
                         std::is_same_v<T, SourceLocalAssignment>) {
      return Read(Reads(op.value));
    } else if constexpr (std::is_same_v<T, SourceLocalStore> ||
                         std::is_same_v<T, SourceModuleStore>) {
      return Read(std::holds_alternative<analysis::Accumulator>(op.value));
    } else if constexpr (std::is_same_v<T, SourceConditionalBreak> ||
                         std::is_same_v<T, SourceConditionalContinue>) {
      if (!op.input) return Read(Reads(op.condition.input));
      return Read(std::visit([](const auto& input) {
        return Reads(SourceExpression{input});
      }, *op.input));
    } else if constexpr (std::is_same_v<T, analysis::NoOperation> ||
                         std::is_same_v<T, SourceBreak> ||
                         std::is_same_v<T, SourceContinue>) {
      return Access::kNone;
    } else {
      static_assert(std::is_same_v<T, SourceStagedCall> ||
                    std::is_same_v<T, SourceArrayLiteral> ||
                    std::is_same_v<T, SourceArrayCommit> ||
                    std::is_same_v<T, SourceArrayCapture> ||
                    std::is_same_v<T, SourceIndexedStore> ||
                    std::is_same_v<T, SourceMethodStore> ||
                    std::is_same_v<T, SourceInitializerCall>);
      return Access::kRead;
    }
  }, node.operation);
}
struct BlockAccess {
  Access first = Access::kNone;
  bool live = false;
  std::optional<std::size_t> incoming;
};
struct BackEdge {
  analysis::BlockId source;
  std::optional<std::size_t> next;
};
bool Charge(std::size_t count, std::size_t width, std::size_t& bytes) {
  if (count > bytes / width) return false;
  bytes -= count * width;
  return true;
}
}  // namespace

std::variant<AccumulatorLiveness, SourceFailure> FindAccumulatorLiveness(
    const AstMethod& method, const SourceAst& ast, std::size_t budget_bytes) {
  const auto invalid = SourceFailure{SourceError::kInvalidEffect, {}};
  const auto exhausted = SourceFailure{SourceError::kResourceLimit, {}};
  if (method.ssa.IsPhased() || ast.regions.catches || ast.argument_region ||
      method.instructions.size() != ast.statements.size()) return invalid;
  const auto count = method.flow.blocks.size();
  std::size_t edges_count = 0;
  for (const auto& block : method.flow.blocks) {
    if (block.successor_count > block.successors.size()) return invalid;
    if (edges_count > budget_bytes / sizeof(BackEdge) ||
        block.successor_count > budget_bytes / sizeof(BackEdge) - edges_count) return exhausted;
    edges_count += block.successor_count;
  }
  auto available = budget_bytes;
  if (!Charge(ast.statements.size(), sizeof(AccumulatorAccess) + sizeof(std::uint8_t), available) ||
      !Charge(count, sizeof(BlockAccess) + sizeof(analysis::BlockId), available) ||
      !Charge(edges_count, sizeof(BackEdge), available)) return exhausted;
  AccumulatorLiveness result;
  result.accesses.resize(ast.statements.size());
  result.after.resize(ast.statements.size());
  std::vector<BlockAccess> blocks(count);
  std::vector<BackEdge> edges;
  std::vector<analysis::BlockId> queue;
  edges.reserve(edges_count);
  queue.reserve(count);
  available = budget_bytes;
  if (!Charge(result.accesses.capacity(), sizeof(AccumulatorAccess), available) ||
      !Charge(result.after.capacity(), sizeof(std::uint8_t), available) ||
      !Charge(blocks.capacity(), sizeof(BlockAccess), available) ||
      !Charge(edges.capacity(), sizeof(BackEdge), available) ||
      !Charge(queue.capacity(), sizeof(analysis::BlockId), available)) return exhausted;
  std::optional<std::size_t> owned_end;
  for (std::size_t i = 0; i < ast.statements.size(); ++i) {
    const auto& node = ast.statements[i];
    if (const auto* call = std::get_if<SourceStagedCall>(&node.operation)) {
      if (call->call_statement.value < i) return invalid;
      owned_end = std::max(owned_end.value_or(i), std::size_t(call->call_statement.value));
    }
    if (const auto* array = std::get_if<SourceArrayLiteral>(&node.operation)) {
      if (array->elements.empty() || array->elements.back().value < i) return invalid;
      owned_end = std::max(owned_end.value_or(i), std::size_t(array->elements.back().value));
    }
    if (owned_end && *owned_end >= ast.statements.size()) return invalid;
    const bool owned = (owned_end && i <= *owned_end) ||
        (ast.super_arguments && i >= ast.super_arguments->first.value &&
         i <= ast.super_arguments->call.value);
    result.accesses[i] = owned ? Access::kRead : Classify(node);
  }
  for (std::size_t b = 0; b < count; ++b) {
    const auto& block = method.flow.blocks[b];
    const auto end = std::size_t(block.first_instruction) + block.instruction_count;
    if (end > ast.statements.size()) return invalid;
    for (auto i = std::size_t(block.first_instruction); i < end; ++i) {
      if (result.accesses[i] != Access::kNone) {
        blocks[b].first = result.accesses[i];
        break;
      }
    }
    for (std::size_t e = 0; e < block.successor_count; ++e) {
      const auto& edge = block.successors[e];
      if (edge.target.value >= count || edge.kind == analysis::FlowEdgeKind::kResume) return invalid;
      edges.push_back({{static_cast<std::uint32_t>(b)}, blocks[edge.target.value].incoming});
      blocks[edge.target.value].incoming = edges.size() - 1;
    }
    if (blocks[b].first == Access::kRead) {
      blocks[b].live = true;
      queue.push_back({static_cast<std::uint32_t>(b)});
    }
  }
  // One monotone false->true transition per block. Propagate only through
  // blocks with no first access; an overwrite kills incoming accumulator state.
  for (std::size_t next = 0; next < queue.size(); ++next) {
    for (auto edge = blocks[queue[next].value].incoming; edge; edge = edges[*edge].next) {
      auto& source = blocks[edges[*edge].source.value];
      if (source.first == Access::kNone && !source.live) {
        source.live = true;
        queue.push_back(edges[*edge].source);
      }
    }
  }
  for (const auto& block : method.flow.blocks) {
    bool live = false;
    for (std::size_t e = 0; e < block.successor_count; ++e)
      live |= blocks[block.successors[e].target.value].live;
    auto i = std::size_t(block.first_instruction) + block.instruction_count;
    while (i > block.first_instruction) {
      --i;
      result.after[i] = live;
      if (result.accesses[i] == Access::kRead) live = true;
      else if (result.accesses[i] == Access::kWrite) live = false;
    }
  }
  return result;
}
}  // namespace abcd::source
