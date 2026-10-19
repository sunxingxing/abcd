#include <new>
#include <stdexcept>
#include <tuple>

#include "abcd/source/scalar_expression.h"
#include "class_self_resource.h"
#include "class_tree.h"
#include "family_file.h"
#include "tree_checks.h"
namespace abcd::source {
namespace {
using namespace analysis;
using Error = ClassSelfInitializationError;
class Quota {
 public:
  Quota(const ClassSelfInitializationInput& input, SemanticBudget budget)
      : initial(budget), left(budget) {
    failure.creator = input.creator.input.Method();
    failure.creation = input.call.creation;
    failure.location = OperationLocation(input.creator.values.graph,
                                         input.call.call, failure.creator);
  }
  void Require(bool ok, Error error) const {
    if (!ok) {
      auto f = failure;
      f.kind = error;
      throw f;
    }
  }
  void Admit(std::size_t n, std::size_t w = 1) {
    Require(w && n <= left.bytes / w, Error::kResourceLimit);
    left.bytes -= n * w;
  }
  void Step(std::uint64_t n = 1) {
    Require(n <= left.work, Error::kWorkLimit);
    left.work -= n;
  }
  template <class T>
  void Reserve(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T));
    v.reserve(n);
    Admit(v.capacity() - n, sizeof(T));
  }
  template <class T, class... E>
  T Take(std::variant<T, E...> result, Error error) {
    if (std::get_if<T>(&result)) return std::get<T>(std::move(result));
    Require(false, ClassSelfResource(result).value_or(error));
    throw failure;
  }
  ClassSelfInitializationCost Cost() const {
    return {initial.bytes - left.bytes, initial.work - left.work};
  }
  SemanticBudget initial, left;
  ClassSelfInitializationFailure failure{Error::kCorrespondence};
};
auto Key(const ClassSelfRequirement& r) {
  return std::tuple{r.owner.family, r.owner.member, r.incoming.depth.value,
                    r.incoming.slot.value};
}
}  // namespace
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfClassDraft(const ClassSelfInitializationInput& input,
                          const ClassSelfInitialization& raw,
                          const ClassSelfScope& scope, const ClassTree& layout,
                          const EvaluationPlan& evaluation,
                          const ClassInitializerTreeDraft& draft,
                          SemanticBudget budget) {
  Quota q(input, budget);
  try {
    const auto verified = q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfScope(input, raw, scope, q.left),
        Error::kCorrespondence);
    q.Admit(verified.bytes);
    q.Step(verified.work);
    q.Require(
        !input.families.empty() && !input.families[0].family.members.empty(),
        Error::kOccurrence);
    const auto& function = input.families[0].family.members[0].function;
    const auto& graph = function.Graph();
    const auto& tree = draft.tree;
    q.failure.initializer = function.Method();
    q.Require(tree.identity == input.creator.input.Identity() &&
                  tree.method == function.Method() &&
                  tree.owner == input.families[0].family.Owner({0}) &&
                  tree.value_revision == graph.revision &&
                  tree.cfg_revision == graph.cfg_revision && tree.class_inner &&
                  tree.class_inner->binding == scope.binding &&
                  tree.class_inner->name.value < tree.names.size() &&
                  tree.names[tree.class_inner->name.value] == layout.name,
              Error::kName);
    q.Require(layout.class_self &&
                  layout.class_self->binding == scope.binding &&
                  layout.class_self->ports == scope.ports &&
                  layout.class_self->cleanup == scope.cleanup &&
                  layout.class_self->exception == scope.initializer_exception &&
                  layout.initializer_site == raw.call,
              Error::kCatchState);
    std::uint64_t owner_end = input.families[0].family.first_owner.value;
    for (const auto& occurrence : input.families) {
      q.Step();
      q.Require(occurrence.family.first_owner.value == owner_end,
                Error::kScope);
      owner_end += occurrence.family.members.size();
      q.Require(owner_end <= std::uint64_t(UINT32_MAX) + 1,
                Error::kResourceLimit);
    }
    q.Require(layout.class_self->first_owner ==
                      input.families[0].family.first_owner &&
                  layout.class_self->owner_end == owner_end,
              Error::kScope);
    q.Step(q.Take<std::uint64_t>(
        VerifyEvaluationPlan(graph, evaluation, q.left),
        Error::kCorrespondence));
    const auto bindings = q.Take<BindingPlan>(
        BuildBindingPlan(graph, evaluation, function.Normal().frame.parameters,
                         q.left),
        Error::kCorrespondence);
    q.Admit(bindings.allocated_bytes);
    q.Step(bindings.work);
    q.Require(evaluation.cells && bindings.cells == draft.bindings.cells &&
                  tree.symbols.size() >= bindings.symbols.size(),
              Error::kCorrespondence);
    std::size_t outer = 0;
    for (std::size_t i = 0; i < evaluation.cells->cells.cells.size(); ++i) {
      q.Step();
      const auto* incoming =
          std::get_if<IncomingCell>(&evaluation.cells->cells.cells[i].storage);
      if (!incoming) continue;
      const auto key =
          std::tuple{0U, 0U, incoming->depth.value, incoming->slot.value};
      const auto found =
          std::lower_bound(scope.requirements.begin(), scope.requirements.end(),
                           key, [&](const auto& r, auto k) {
                             q.Step();
                             return Key(r) < k;
                           });
      q.Require(found != scope.requirements.end() && Key(*found) == key &&
                    i < bindings.cells.size(),
                Error::kScope);
      const auto symbol = bindings.cells[i];
      q.Require(symbol.value < tree.symbols.size(), Error::kScope);
      const auto& binding = tree.symbols[symbol.value];
      if (found->kind == ClassSelfRequirementKind::kClassInner) {
        q.Require(binding.kind == BindingKind::kClassInner &&
                      binding.scope == tree.class_inner->scope &&
                      binding.name == tree.class_inner->name &&
                      binding.declaration == DeclarationKind::kConst,
                  Error::kScope);
      } else {
        q.Require(outer < tree.capture_requirements.size(), Error::kScope);
        const auto& actual = tree.capture_requirements[outer++];
        q.Require(actual.symbol == symbol &&
                      actual.storage.depth == incoming->depth &&
                      actual.storage.slot.value == incoming->slot.value &&
                      actual.domain == found->domain &&
                      binding.kind == BindingKind::kCaptureRequirement,
                  Error::kStorage);
      }
    }
    q.Require(outer == tree.capture_requirements.size(), Error::kScope);
    const auto syntax = q.Take<TreeStructure>(
        VerifyClassInitializerStructure(tree, q.left), Error::kName);
    q.Admit(syntax.allocated_bytes);
    q.Step(syntax.work);
    // Summarize the original-site DAG once, never walk a subtree per read.
    struct ReadOrigin {
      std::optional<std::size_t> read;
      bool mixed = false;
    };
    std::vector<ReadOrigin> sites, origins;
    q.Reserve(sites, graph.input.sites.size());
    sites.resize(graph.input.sites.size());
    q.Reserve(origins, tree.origins.size());
    origins.resize(tree.origins.size());
    std::vector<std::uint32_t> seen;
    q.Reserve(seen, scope.references.size());
    seen.resize(scope.references.size());
    for (std::size_t r = 0; r < scope.references.size(); ++r) {
      q.Step();
      const auto& ref = scope.references[r];
      if (ref.owner.family || ref.owner.member) continue;
      for (const auto op : {ref.read, ref.guard}) {
        q.Require(op.value < graph.ops.size(), Error::kGuard);
        const auto at =
            graph.origins[graph.ops[op.value].origin.value].site.value;
        q.Require(at < sites.size() && (!sites[at].read || sites[at].read == r),
                  Error::kGuard);
        sites[at].read = r;
      }
    }
    for (std::size_t i = 0; i < tree.origins.size(); ++i) {
      q.Step();
      const auto& origin = tree.origins[i];
      if (const auto* at = std::get_if<OriginalSite>(&origin)) {
        q.Require(at->site.value < sites.size(), Error::kCorrespondence);
        origins[i] = sites[at->site.value];
      } else if (const auto* combined = std::get_if<CombinedOrigin>(&origin)) {
        q.Require(combined->first_part <= tree.origin_parts.size() &&
                      combined->part_count <=
                          tree.origin_parts.size() - combined->first_part,
                  Error::kCorrespondence);
        for (std::uint32_t p = 0; p < combined->part_count; ++p) {
          q.Step();
          const auto part = tree.origin_parts[combined->first_part + p];
          q.Require(part.value < i, Error::kCorrespondence);
          const auto source = origins[part.value];
          origins[i].mixed |= source.mixed;
          if (source.read) {
            if (origins[i].read && origins[i].read != source.read)
              origins[i].mixed = true;
            else
              origins[i].read = source.read;
          }
        }
      }
    }
    for (const auto& expression : tree.expressions) {
      q.Step();
      const auto* symbol = std::get_if<SymbolExpr>(&expression.expression);
      if (!symbol ||
          tree.symbols[symbol->symbol.value].kind != BindingKind::kClassInner)
        continue;
      q.Require(expression.origin.value < origins.size(),
                Error::kCorrespondence);
      const auto source = origins[expression.origin.value];
      q.Require(source.read && !source.mixed, Error::kGuard);
      const auto& ref = scope.references[*source.read];
      const auto cell = evaluation.cells->cells.operations[ref.read.value];
      q.Require(cell && symbol->symbol == bindings.cells[cell->value] &&
                    ++seen[*source.read] == 1,
                Error::kGuard);
    }
    for (std::size_t r = 0; r < scope.references.size(); ++r) {
      q.Step();
      const auto& ref = scope.references[r];
      if (ref.owner.family || ref.owner.member) continue;
      q.failure.location =
          OperationLocation(graph, ref.guard, function.Method());
      q.failure.read = ref.read;
      q.failure.guard = ref.guard;
      q.Require(seen[r] == 1, Error::kGuard);
    }
    return q.Cost();
  } catch (const ClassSelfInitializationFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
std::variant<ClassSelfClassDraft, ClassSelfInitializationFailure>
BuildClassSelfClassDraft(const ClassSelfInitializationInput& input,
                         const ClassSelfInitialization& raw,
                         const ClassSelfScope& scope, ClassTree& layout,
                         FileFunction& creator, SemanticBudget budget,
                         const RecordModuleContext* modules) {
  Quota q(input, budget);
  try {
    q.Require(&creator == &input.creator &&
                  layout.operation == raw.call.creation &&
                  layout.identity == input.creator.input.Identity() &&
                  IsSourceIdentifier(std::u16string_view{layout.name}),
              Error::kOccurrence);
    auto verified = q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfScope(input, raw, scope, q.left),
        Error::kCorrespondence);
    q.Admit(verified.bytes);
    q.Step(verified.work);
    std::vector<CellDemand> demands;
    q.Reserve(demands, scope.requirements.size());
    for (const auto& requirement : scope.requirements) {
      q.Step();
      if (requirement.owner.member) continue;
      demands.push_back(
          {requirement.creation,
           {requirement.incoming.depth, requirement.incoming.slot},
           requirement.domain,
           {}});
    }
    auto cells = q.Take<CellPlan>(
        BuildDemandedCellPlan(creator.values.graph,
                              *creator.values.environments, demands, q.left),
        Error::kStorage);
    q.Admit(cells.allocated_bytes);
    q.Step(cells.work);
    creator.values.cells.emplace(std::move(cells));
    auto order = q.Take<ClassSelfOrder>(
        ClassSelfOrder::BindInitialization(input, raw, scope, q.left),
        Error::kOrder);
    q.Admit(order.AllocatedBytes());
    q.Step(order.Work());
    const auto& family = input.families[0].family;
    const auto& function = family.members[0].function;
    q.Require(!function.Phase(), Error::kScope);
    const auto& body = function.Normal();
    q.failure.initializer = body.input.Method();
    ClassTreeInitialization initialization{
        scope.binding,      {}, {}, scope.initializer_exception,
        family.first_owner, 0};
    q.Reserve(initialization.ports, scope.ports.size());
    for (const auto& port : scope.ports) {
      q.Step();
      initialization.ports.push_back(port);
    }
    q.Reserve(initialization.cleanup, scope.cleanup.size());
    for (const auto& cleanup : scope.cleanup) {
      q.Step();
      initialization.cleanup.push_back(cleanup);
    }
    std::uint64_t next = family.first_owner.value;
    for (const auto& occurrence : input.families) {
      q.Step();
      q.Require(occurrence.family.first_owner.value == next, Error::kScope);
      next += occurrence.family.members.size();
      q.Require(next <= std::uint64_t(UINT32_MAX) + 1, Error::kResourceLimit);
    }
    initialization.owner_end = next;
    layout.class_self.emplace(std::move(initialization));
    layout.initializer_site = raw.call;
    auto syntax = q.Take<ResolvedSyntax>(
        ResolveFunctionSyntax(input.file, input.header, input.index,
                              input.families[0].context, body.body,
                              body.values.graph, family.Owner({0}), q.left),
        Error::kName);
    q.Admit(syntax.allocated_bytes);
    q.Step(syntax.work);
    std::optional<ModuleAliases> aliases;
    if (modules) {
      aliases.emplace(q.Take<ModuleAliases>(
          BindModuleAliases(
              input.file, input.header, input.index, body.input, body.values,
              *modules, {},
              NameId{static_cast<std::uint32_t>(syntax.syntax.names.size())},
              q.left),
          Error::kScope));
      q.Admit(aliases->allocated_bytes);
      q.Step(aliases->work);
      q.Reserve(syntax.syntax.modules, aliases->aliases.size());
      for (const auto& alias : aliases->aliases) {
        q.Step();
        syntax.syntax.modules.push_back(alias);
      }
      const auto count = syntax.syntax.names.size() + aliases->names.size();
      q.Admit(count, sizeof(std::u16string));
      syntax.syntax.names.reserve(count);
      q.Admit(syntax.syntax.names.capacity() - count, sizeof(std::u16string));
      for (const auto& name : aliases->names) {
        q.Step(name.size() + 1);
        q.Admit(name.size() + 1, sizeof(char16_t));
        syntax.syntax.names.push_back(name);
        q.Admit(syntax.syntax.names.back().capacity() - name.size(),
                sizeof(char16_t));
      }
    }
    const NameId class_name{
        static_cast<std::uint32_t>(syntax.syntax.names.size())};
    q.Admit(syntax.syntax.names.size() + 1, sizeof(std::u16string));
    syntax.syntax.names.reserve(syntax.syntax.names.size() + 1);
    q.Admit(syntax.syntax.names.capacity() - (class_name.value + 1),
            sizeof(std::u16string));
    q.Admit(layout.name.size() + 1, sizeof(char16_t));
    syntax.syntax.names.push_back(layout.name);
    q.Admit(syntax.syntax.names.back().capacity() - layout.name.size(),
            sizeof(char16_t));
    const auto control = q.Take<ControlPlan>(
        BuildControlPlan(body.values.graph, q.left), Error::kCorrespondence);
    q.Admit(control.allocated_bytes);
    q.Step(control.work);
    const auto* storage = function.Cells();
    auto evaluation = q.Take<EvaluationPlan>(
        BuildEvaluationPlan(
            body.values.graph, q.left, IncomingCells::kConditional,
            family.members[0].children,
            aliases ? std::span<const ModuleEvaluation>(aliases->evaluations)
                    : std::span<const ModuleEvaluation>{},
            storage ? std::span<const CellDemand>(storage->demands)
                    : std::span<const CellDemand>{},
            {}, ClosureWrites::kLanguage, {}, {},
            storage ? std::span<const IncomingCell>(storage->entry_demands)
                    : std::span<const IncomingCell>{},
            nullptr, {},
            {body.input.Identity().contract, syntax.View(),
             syntax.syntax.names}),
        Error::kCorrespondence);
    q.Admit(evaluation.allocated_bytes);
    q.Step(evaluation.work);
    const ClassInitializerTreeInput source{input, raw, scope, 0, 0, class_name};
    const auto constants = syntax.View();
    auto tree = q.Take<ClassInitializerTreeDraft>(
        BuildClassInitializerTreeDraft(body.values.graph, control, evaluation,
                                       std::move(syntax.syntax), constants,
                                       source, q.left),
        Error::kScope);
    q.Admit(tree.allocated_bytes);
    q.Step(tree.work);
    verified = q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfClassDraft(input, raw, scope, layout, evaluation, tree,
                                  q.left),
        Error::kCorrespondence);
    q.Admit(verified.bytes);
    q.Step(verified.work);
    const auto cost = q.Cost();
    return ClassSelfClassDraft{std::move(tree), std::move(order),
                               std::move(evaluation), cost.bytes, cost.work};
  } catch (const ClassSelfInitializationFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
}  // namespace abcd::source
