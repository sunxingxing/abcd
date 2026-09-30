#include "class_initializers.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<ClassProjection, ClassInitializerError> ProjectClassMembers(
    const analysis::ValueProgram& graph, std::span<const ClassInitializerCall> requested,
    analysis::SemanticBudget budget, std::span<const ClassDefinitionSite> definitions) {
  using namespace analysis;
  using Error = ClassInitializerError;
  const auto initial = budget;
  try {
    const auto require = [](bool ok, Error error = Error::kInput) { if (!ok) throw error; };
    const auto step = [&](std::size_t n = 1) {
      require(n <= budget.work, Error::kWorkLimit); budget.work -= n;
    };
    const auto admit = [&](std::size_t n, std::size_t width) {
      require(n <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= n * width;
    };
    ClassProjection result;
    auto member_result = ProjectClassDefinitions(graph, definitions, budget);
    if (const auto* error = std::get_if<ClassDefinitionError>(&member_result)) {
      switch (*error) {
        case ClassDefinitionError::kInput: return Error::kInput;
        case ClassDefinitionError::kFlow: return Error::kFlow;
        case ClassDefinitionError::kResourceLimit: return Error::kResourceLimit;
        case ClassDefinitionError::kWorkLimit: return Error::kWorkLimit;
      }
    }
    auto member_projection = std::get<ClassDefinitionProjection>(std::move(member_result));
    admit(member_projection.allocated_bytes, 1); step(member_projection.work);
    result.owners = std::move(member_projection.owners);
    result.prototypes = std::move(member_projection.prototypes);
    if (requested.empty()) {
      result.allocated_bytes = initial.bytes - budget.bytes; result.work = initial.work - budget.work;
      return result;
    }
    auto found = FindClassInitializerCalls(graph, budget);
    if (const auto* error = std::get_if<Error>(&found)) return *error;
    const auto& candidates = std::get<ClassInitializerCalls>(found);
    admit(candidates.allocated_bytes, 1); step(candidates.work);
    // Prefix counts and one ownership entry per class avoid rescanning an
    // arbitrarily long corridor for each method. O(ops + uses + calls log calls).
    admit(graph.ops.size() + 1, sizeof(std::size_t));
    std::vector<std::size_t> effects(graph.ops.size() + 1);
    admit(effects.capacity() - effects.size(), sizeof(std::size_t));
    admit(graph.ops.size(), sizeof(std::uint8_t));
    std::vector<std::uint8_t> claimed(graph.ops.size());
    admit(claimed.capacity() - claimed.size(), sizeof(std::uint8_t));
    admit(graph.ops.size(), sizeof(std::optional<OpId>));
    result.owners.resize(graph.ops.size());
    admit(result.owners.capacity() - result.owners.size(), sizeof(std::optional<OpId>));
    admit(graph.ops.size(), sizeof(OpId)); result.prototypes.reserve(graph.ops.size());
    admit(result.prototypes.capacity() - graph.ops.size(), sizeof(OpId));
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      require(op.block.value < graph.blocks.size());
      const auto& block = graph.blocks[op.block.value];
      require(i >= block.first_op.value && i - block.first_op.value < block.op_count &&
          op.order == i - block.first_op.value);
      effects[i + 1] = effects[i] + (op.life == ValueLife::kLive &&
          (op.normal != LanguageEffect::kNone || op.before_throw != LanguageEffect::kNone ||
           op.exceptional != ExceptionalCompletion::kImpossible));
    }
    std::optional<OpId> previous;
    for (const auto& call : requested) {
      step(); require(!previous || *previous < call.method); previous = call.method;
      const auto actual = std::lower_bound(candidates.calls.begin(), candidates.calls.end(), call.method,
          [&](const auto& candidate, auto method) { step(); return candidate.method < method; });
      require(actual != candidates.calls.end() && *actual == call);
      require(call.creation < call.method && call.method < call.call && !claimed[call.creation.value]);
      claimed[call.creation.value] = 1;
      const auto& creation = graph.ops[call.creation.value];
      const auto& invoke = graph.ops[call.call.value];
      require(creation.block == invoke.block);
      const auto& block = graph.blocks[creation.block.value];
      require(call.creation.value >= block.first_op.value &&
          call.call.value - block.first_op.value < block.op_count);
      std::size_t reads = 0;
      const auto project = [&](OpId id) {
        require(!result.owners[id.value]); result.owners[id.value] = call.creation;
      };
      project(call.method); project(call.call);
      for (auto use = graph.values[call.receiver.value].first_use; use; use = graph.uses[use->value].next) {
        step(); const auto& occurrence = graph.uses[use->value];
        const auto* owner = std::get_if<OpId>(&occurrence.owner);
        require(owner != nullptr, Error::kFlow);
        if (*owner == call.method || *owner == call.call) continue;
        const auto& op = graph.ops[owner->value];
        // Different blocks may observe the class only after a separately
        // established control relation; that extension is not guessed here.
        require(op.block == invoke.block, Error::kFlow);
        if (op.order > invoke.order) continue;
        require(*owner > call.creation && *owner < call.call &&
            std::holds_alternative<ValueNamedRead>(op.operation) && op.use_count == 1 &&
            occurrence.role == OperandRole::kBase && op.result &&
            !graph.values[op.result->value].first_use, Error::kFlow);
        // Named-read spelling remains an explicit source-level obligation.
        require(effects[owner->value + 1] != effects[owner->value]);
        project(*owner); result.prototypes.push_back(*owner); ++reads;
      }
      require(effects[call.method.value + 1] != effects[call.method.value] &&
          effects[call.call.value + 1] != effects[call.call.value]);
      constexpr std::size_t kMethodAndCall = 2;
      require(effects[call.call.value + 1] - effects[call.creation.value + 1] == reads + kMethodAndCall,
          Error::kFlow);
    }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
