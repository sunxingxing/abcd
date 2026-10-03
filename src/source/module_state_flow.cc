#include "module_state_flow.h"

#include <new>
#include <stdexcept>
#include <type_traits>

#include "../analysis/register_value_rule.h"
#include "../analysis/value_flow.h"
namespace abcd::source {
std::variant<analysis::ModuleInitialization, ModuleGuardError>
BuildWorkingModuleState(const analysis::ValueProgram& graph,
                        analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ModuleGuardError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error error = Error::kRead) {
      if (!ok) throw error;
    };
    const auto step = [&](std::uint64_t n = 1) {
      require(n <= budget.work, Error::kWorkLimit);
      budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t width = 1) {
      require(n <= budget.bytes / width, Error::kResourceLimit);
      budget.bytes -= n * width;
    };
    require(graph.phases.empty(), Error::kUnsupportedProof);
    auto verified = VerifyValueFlow(graph, budget);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&verified)) {
      if (*error == ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
      if (*error == ValueAvailabilityError::kResourceLimit)
        throw Error::kResourceLimit;
      throw Error::kRead;
    }
    const auto& proof = std::get<ValueFlowProof>(verified);
    step(proof.work);
    admit(proof.allocated_bytes);
    admit(graph.ops.size(), sizeof(ModuleStateSite));
    std::vector<ModuleStateSite> sites(graph.ops.size());
    admit(sites.capacity() - graph.ops.size(), sizeof(ModuleStateSite));
    NormalFlow flow;
    admit(graph.blocks.size(), sizeof(FlowBlock));
    flow.blocks.resize(graph.blocks.size());
    admit(flow.blocks.capacity() - graph.blocks.size(), sizeof(FlowBlock));
    for (std::size_t b = 0; b < graph.blocks.size(); ++b) {
      step();
      const auto& block = graph.blocks[b];
      auto& region = flow.blocks[b];
      region.first_instruction = block.first_op.value;
      region.instruction_count = block.op_count;
      require(block.edge_count <= kMaxNormalSuccessors);
      region.successor_count = block.edge_count;
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        step();
        const auto& edge = graph.edges[block.first_edge.value + e];
        require(edge.kind == FlowEdgeKind::kTaken ||
                edge.kind == FlowEdgeKind::kFallthrough);
        region.successors[e] = {{edge.target.value}, edge.kind};
      }
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step();
      const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive) continue;
      require(op.origin.value < graph.origins.size(), Error::kOrigin);
      sites[i].pc = graph.origins[op.origin.value].pc;
      if (const auto* read = std::get_if<ValueModuleRead>(&op.operation))
        sites[i].snapshot = read->space == ModuleSpace::kLocal;
      const auto* write = std::get_if<ValueModuleWrite>(&op.operation);
      if (!write) continue;
      require(!op.result && op.use_count == 1 &&
              op.first_use.value < graph.uses.size());
      const auto& use = graph.uses[op.first_use.value];
      const auto* owner = std::get_if<OpId>(&use.owner);
      require(owner && owner->value == i &&
              use.role == OperandRole::kStoredValue &&
              use.purpose == ReadPurpose::kOperand);
      const auto language = std::visit(
          [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ValueId>)
              return value.value < graph.values.size() &&
                     graph.values[value.value].life == ValueLife::kLive &&
                     graph.values[value.value].domain.IsLanguage();
            else
              return detail::DescribeScalarValue(
                         {Accumulator{}, ScalarValue{value}})
                  .seed.IsLanguage();
          },
          use.operand);
      require(language);
      sites[i].store = write->slot;
    }
    auto built = BuildModuleInitialization(sites, flow, {}, budget);
    if (const auto* error = std::get_if<ModuleInitializationError>(&built)) {
      if (*error == ModuleInitializationError::kWorkLimit)
        throw Error::kWorkLimit;
      if (*error == ModuleInitializationError::kResourceLimit)
        throw Error::kResourceLimit;
      throw Error::kRead;
    }
    auto result = std::get<ModuleInitialization>(std::move(built));
    result.allocated_bytes += initial.bytes - budget.bytes;
    result.work += initial.work - budget.work;
    return result;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
