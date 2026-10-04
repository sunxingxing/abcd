#include "class_self_order.h"
#include "class_initializer_file.h"
#include "literal_class_fields.h"

#include <new>
#include <stdexcept>

namespace abcd::source {
std::span<const ClassSelfStore> ClassSelfOrder::Stores(const analysis::ValueProgram& graph) const {
  if (graph.ops.data() != operations_ || graph.values.data() != values_ ||
      graph.ops.size() != operation_count_ || graph.values.size() != value_count_ ||
      graph.revision != revision_ || graph.cfg_revision != cfg_) return {};
  return stores_;
}
std::span<const std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> ClassSelfOrder::Prefixes(
    const FunctionInput& input, const analysis::ValueProgram& graph) const {
  if (Stores(graph).empty() || input.Input().instructions.data() != instructions_ ||
      input.Proof().locals.cells.data() != cells_) return {};
  return prefixes_;
}
std::variant<ClassSelfOrder, ClassSelfOrderError> ClassSelfOrder::Build(const FileFunction& parent,
    std::span<const ClassInitializerWitness> witnesses, analysis::SemanticBudget budget) {
  using namespace analysis;
  using Error = ClassSelfOrderError;
  const auto initial = budget;
  try {
    const auto require = [](bool condition, Error error) { if (!condition) throw error; };
    const auto step = [&](std::uint64_t count = 1) { require(count <= budget.work,Error::kWorkLimit); budget.work -= count; };
    const auto admit = [&](std::size_t count, std::size_t width) { require(count <= budget.bytes/width,Error::kResourceLimit); budget.bytes -= count*width; };
    const auto& graph = parent.values.graph;
    require(parent.values.environments && parent.values.cells &&
        parent.values.identity == parent.input.Identity() && parent.values.method == parent.input.Method(),Error::kOwner);
    auto found = FindClassSelfStores(graph,*parent.values.environments,*parent.values.cells,
        parent.values.initializers.calls,budget);
    if (const auto* error = std::get_if<ClassSelfError>(&found)) {
      if (*error == ClassSelfError::kResourceLimit) return Error::kResourceLimit;
      if (*error == ClassSelfError::kWorkLimit) return Error::kWorkLimit;
      return Error::kInput;
    }
    auto& candidates = std::get<ClassSelfStores>(found);
    admit(candidates.allocated_bytes,1); step(candidates.work);
    std::vector<std::optional<std::size_t>> indexed;
    admit(graph.ops.size(),sizeof(decltype(indexed)::value_type)); step(graph.ops.size()); indexed.resize(graph.ops.size());
    admit(indexed.capacity()-indexed.size(),sizeof(decltype(indexed)::value_type));
    for (std::size_t i = 0; i < candidates.stores.size(); ++i) {
      step(); const auto id = candidates.stores[i].initializer;
      require(id.value < indexed.size() && !indexed[id.value],Error::kInput); indexed[id.value] = i;
    }
    ClassSelfOrder result; admit(1,sizeof(result));
    admit(witnesses.size(),sizeof(ClassSelfStore)); result.stores_.reserve(witnesses.size());
    admit(result.stores_.capacity()-witnesses.size(),sizeof(ClassSelfStore));
    for (const auto& witness : witnesses) {
      step(); const auto& call = witness.call; const auto& body = witness.function.function;
      const auto& context = witness.function.context;
      require(call.call.value < indexed.size() && indexed[call.call.value] && call.method.value < graph.ops.size(),Error::kInput);
      const auto& store = candidates.stores[*indexed[call.call.value]];
      require(store.creation == call.creation && graph.ops[call.creation.value].result == call.receiver,Error::kInput);
      const auto& invocation = graph.ops[call.call.value];
      require(invocation.use_count == 2,Error::kInput);
      const auto* callee = std::get_if<ValueId>(&graph.uses[invocation.first_use.value].operand);
      const auto* definition = callee ? std::get_if<OpResult>(&graph.values[callee->value].origin) : nullptr;
      require(definition && definition->op == call.method,Error::kInput);
      const auto* site = context.CreationSite();
      const auto& operation = graph.ops[call.method.value];
      const auto* method = std::get_if<ValueFunctionCreation>(&operation.operation);
      require(site && method && operation.origin.value < graph.origins.size() &&
          context.Identity() == parent.input.Identity() && site->creator == parent.input.Method() &&
          site->pc == graph.origins[operation.origin.value].pc && context.Target() == body.input.Method() &&
          body.input.MatchesOwner(context) && body.input.Identity() == parent.input.Identity() &&
          body.input.Record() == parent.input.Record() &&
          body.values.identity == body.input.Identity() && body.values.method == body.input.Method(),Error::kOwner);
      require(site->creation.kind == method->kind && site->creation.method == method->method &&
          site->creation.length.value == method->length && site->creation.cache_slot == method->cache_slot &&
          site->creation.home_object == method->home_object && context.Callable() == CallableKind::kMethod &&
          !context.Length() && !context.Frame().parameters && body.input.Captures().empty(),Error::kOwner);
      auto checked = LiteralClassFields::Build(body.values.graph,budget);
      if (const auto* error = std::get_if<LiteralClassFieldError>(&checked)) {
        if (*error == LiteralClassFieldError::kResourceLimit) return Error::kResourceLimit;
        if (*error == LiteralClassFieldError::kWorkLimit) return Error::kWorkLimit;
        return Error::kObservation;
      }
      const auto& fields = std::get<LiteralClassFields>(checked);
      admit(fields.AllocatedBytes(),1); step(fields.Work());
      result.stores_.push_back(store); indexed[call.call.value].reset();
    }
    const auto& input = parent.input;
    const auto& locals = input.Proof().locals;
    admit(locals.cells.size(),sizeof(decltype(result.prefixes_)::value_type)); step(locals.cells.size());
    result.prefixes_.resize(locals.cells.size());
    admit(result.prefixes_.capacity()-result.prefixes_.size(),sizeof(decltype(result.prefixes_)::value_type));
    if (parent.module_capture_prefix) {
      const auto inherited = parent.module_capture_prefix->For(input);
      require(inherited.empty() || inherited.size() == result.prefixes_.size(),Error::kOwner);
      for (std::size_t i = 0; i < inherited.size(); ++i) { step(); result.prefixes_[i] = inherited[i]; }
    }
    std::vector<std::optional<LocalCellId>> stored;
    admit(input.Input().instructions.size(),sizeof(decltype(stored)::value_type)); step(input.Input().instructions.size());
    stored.resize(input.Input().instructions.size()); admit(stored.capacity()-stored.size(),sizeof(decltype(stored)::value_type));
    for (const auto& site : locals.stores) {
      step(); require(site.instruction.value < stored.size() && !stored[site.instruction.value],Error::kInput);
      stored[site.instruction.value] = site.cell;
    }
    for (const auto& store : result.stores_) {
      step();
      const auto allocation = graph.origins[graph.ops[store.allocation.value].origin.value].site.value;
      const auto at = graph.origins[graph.ops[store.store.value].origin.value].site.value;
      const auto capture = graph.origins[graph.ops[store.creation.value].origin.value].site.value;
      require(at < stored.size() && stored[at] && stored[at]->value < locals.cells.size(),Error::kInput);
      require(capture < input.Input().instructions.size() && capture < graph.input.sites.size() &&
          graph.input.sites[capture] == store.creation &&
          std::holds_alternative<ClassCreation>(input.Input().instructions[capture].operation),Error::kInput);
      const auto cell = *stored[at]; const auto& local = locals.cells[cell.value];
      require(local.environment.value < locals.allocations.size() && locals.allocations[local.environment.value] == InstructionId{allocation} &&
          input.Input().instructions[at].pc == graph.origins[graph.ops[store.store.value].origin.value].pc,Error::kInput);
      result.prefixes_[cell.value] = LocalAccessPlan::UnexposedInitialization{{allocation},{at},InstructionId{capture}};
    }
    result.instructions_ = input.Input().instructions.data(); result.cells_ = locals.cells.data();
    result.operations_ = graph.ops.data(); result.values_ = graph.values.data();
    result.operation_count_ = graph.ops.size(); result.value_count_ = graph.values.size();
    result.revision_ = graph.revision; result.cfg_ = graph.cfg_revision;
    result.bytes_ = sizeof(result)+result.stores_.capacity()*sizeof(ClassSelfStore) +
        result.prefixes_.capacity()*sizeof(decltype(result.prefixes_)::value_type);
    result.work_ = initial.work-budget.work; return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
