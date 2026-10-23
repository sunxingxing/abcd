#include "instruction_sources.h"
#include "phased_body.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = InstructionSourceError;
void Require(bool ok, Error error) { if (!ok) throw error; }
class SourceIndex {
 public:
  explicit SourceIndex(analysis::SemanticBudget budget) : initial_(budget), left_(budget) {}
  InstructionSources Build(const RouteInputIdentity& identity, abc::EntityOffset method,
      std::span<const analysis::SemanticInstruction> instructions, const analysis::ValueProgram& graph,
      const FunctionTree& tree,
      std::span<const std::optional<analysis::EnvironmentRef>> before = {}, const CleanupBody* cleanup = nullptr) {
    Require(before.empty() || before.size() == instructions.size(), Error::kCoverage);
    Require(identity == tree.identity && method == tree.method, Error::kIdentity);
    Require(graph.revision == tree.value_revision && graph.cfg_revision == tree.cfg_revision, Error::kRevision);
    Require(graph.origins.size() == graph.ops.size() && tree.origins.size() >= graph.origins.size(), Error::kOrigin);
    if (cleanup) {
      Require(!before.empty() && cleanup->Normal().instructions.size() == instructions.size(), Error::kIdentity);
      for (std::size_t i = 0; i < instructions.size(); ++i) {
        Step(); Require(cleanup->Normal().instructions[i].offset == instructions[i].pc, Error::kOrigin);
        const auto kind = cleanup->Kind({static_cast<std::uint32_t>(i)});
        if (!kind) continue;
        Require(!before[i], Error::kPlacement);
        if (*kind == CleanupInstructionKind::kPop)
          Require(std::holds_alternative<analysis::PopEnvironment>(instructions[i].operation), Error::kOrigin);
        else if (*kind == CleanupInstructionKind::kMove)
          Require(std::holds_alternative<analysis::ScalarAssignment>(instructions[i].operation), Error::kOrigin);
        else {
          const auto* thrown = std::get_if<analysis::ThrowOperation>(&instructions[i].operation);
          Require(thrown && thrown->kind==analysis::ThrowKind::kValue && std::holds_alternative<analysis::Accumulator>(thrown->value), Error::kOrigin);
        }
      }
    }
    InstructionSources result;
    result.input_sites = instructions.size();
    Array(result.occurrences, graph.ops.size());
    std::vector<std::uint8_t> seen, origins;
    std::vector<std::optional<InstructionTarget>> targets;
    Array(seen, instructions.size()); Array(origins, graph.origins.size()); Array(targets, graph.ops.size());
    for (const auto& placement : tree.operations) {
      Step();
      Require(placement.operation.value < graph.ops.size(), Error::kPlacement);
      Require(graph.ops[placement.operation.value].life == analysis::ValueLife::kLive, Error::kPlacement);
      std::visit([&](auto node) {
        Check(node, tree);
        Set(targets[placement.operation.value], node);
      }, placement.node);
    }
    for (const auto& placement : tree.signature) {
      Step();
      Require(placement.operation.value < graph.ops.size(), Error::kPlacement);
      const auto& op = graph.ops[placement.operation.value];
      Require(op.life == analysis::ValueLife::kRemoved && placement.origin.value == op.origin.value, Error::kPlacement);
      Check(placement.parameter, tree);
      Set(targets[placement.operation.value], placement.parameter);
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      Step(); const auto& op = graph.ops[i];
      Require(op.origin.value < graph.origins.size() && !origins[op.origin.value], Error::kOrigin);
      origins[op.origin.value] = 1;
      const auto& origin = graph.origins[op.origin.value];
      Require(origin.site.value < instructions.size(), Error::kCoverage);
      seen[origin.site.value] = 1;
      const auto& instruction = instructions[origin.site.value];
      Require(origin.pc == instruction.pc && origin.original == instruction.origin, Error::kOrigin);
      auto& target = targets[i];
      const auto& tree_origin = tree.origins[op.origin.value];
      const bool dead_phase = !graph.phases.empty() &&
          graph.phases[op.block.value].reachability == analysis::PhaseReachability::kUnreachable;
      if ((!before.empty() && !before[origin.site.value]) || dead_phase) {
        // Normal input reachability, not optimizer purity or a recovery gap.
        // Its original PC remains evidence without any generated syntax range.
        Require(origin.disposition == analysis::OriginDisposition::kRetained &&
            op.life == analysis::ValueLife::kRemoved && !target, Error::kPlacement);
        const auto* source = std::get_if<OriginalSite>(&tree_origin);
        Require(source && Same(*source, origin), Error::kOrigin);
        const auto kind = cleanup ? cleanup->Kind({origin.site.value}) : std::nullopt;
        if (kind) target = CleanupInstruction{*kind};
        else target = UnreachableInstruction{};
      } else if (origin.disposition == analysis::OriginDisposition::kElidedPure) {
        Require(op.life == analysis::ValueLife::kRemoved && !target, Error::kPlacement);
        const auto* source = std::get_if<ElidedOrigin>(&tree_origin);
        Require(source && Same(*source, origin), Error::kOrigin);
        target = ElidedInstruction{};
      } else {
        Require(origin.disposition == analysis::OriginDisposition::kRetained, Error::kOrigin);
        const auto* source = std::get_if<OriginalSite>(&tree_origin);
        Require(source && Same(*source, origin), Error::kOrigin);
        Require(target.has_value(), Error::kCoverage);
        Require((op.life == analysis::ValueLife::kRemoved) ==
            std::holds_alternative<ParameterId>(*target), Error::kPlacement);
      }
      result.occurrences[i] = {origin.site, {op.origin.value}, *target};
    }
    for (const auto present : seen) { Step(); Require(present != 0, Error::kCoverage); }
    result.allocated_bytes = initial_.bytes - left_.bytes;
    result.work = initial_.work - left_.work;
    return result;
  }
 private:
  template<class T> static bool Same(const T& source, const analysis::ValueOrigin& origin) {
    return source.site == origin.site && source.pc == origin.pc && source.original == origin.original;
  }
  static void Set(std::optional<InstructionTarget>& target, InstructionTarget value) {
    Require(!target, Error::kPlacement); target = value;
  }
  static void Check(ExprId node, const FunctionTree& tree) { Require(node.value < tree.expressions.size(), Error::kPlacement); }
  static void Check(StmtId node, const FunctionTree& tree) { Require(node.value < tree.statements.size(), Error::kPlacement); }
  static void Check(AnchorId node, const FunctionTree& tree) { Require(node.value < tree.anchors.size(), Error::kPlacement); }
  static void Check(PropertyId node, const FunctionTree& tree) { Require(node.value < tree.properties.size(), Error::kPlacement); }
  static void Check(ParameterId node, const FunctionTree& tree) {
    Require(node.value < tree.parameters.size() && tree.parameters[node.value].initializer, Error::kPlacement);
  }
  void Step(std::uint64_t count = 1) { Require(count <= left_.work, Error::kWorkLimit); left_.work -= count; }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Require(count <= left_.bytes / sizeof(T), Error::kResourceLimit);
    left_.bytes -= count * sizeof(T); Step(count); values.resize(count);
    const auto extra = values.capacity() - count;
    Require(extra <= left_.bytes / sizeof(T), Error::kResourceLimit); left_.bytes -= extra * sizeof(T);
  }
  analysis::SemanticBudget initial_, left_;
};
}  // namespace
std::variant<InstructionSources, InstructionSourceError> BuildInstructionSources(
    const FunctionInput& input, const analysis::ValueProgram& graph,
    const FunctionTree& tree, analysis::SemanticBudget budget, const CleanupBody* cleanup) {
  try { return SourceIndex(budget).Build(input.Identity(), input.Method(), input.Input().instructions, graph, tree,
        input.Proof().environments.before, cleanup);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<InstructionSources, InstructionSourceError> BuildInstructionSources(
    const PhasedBody& input, const abc::MethodBody& body,
    const FunctionTree& tree, analysis::SemanticBudget budget) {
  try {
    Require(tree.signature.empty(), Error::kPlacement);
    return SourceIndex(budget).Build(input.Identity(), input.Target(),
        input.TreeInput(body).input.flow.instructions, input.Graph(), tree);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
