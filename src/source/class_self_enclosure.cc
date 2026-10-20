#include <algorithm>
#include <new>
#include <stdexcept>

#include "abcd/source/module_enclosure.h"
#include "class_inner_scope.h"
#include "class_self_deferred.h"
#include "class_self_resource.h"
#include "module_aliases.h"
#include "tree_constants.h"
#include "tree_structure.h"

namespace abcd::source {
namespace {
using Error = ClassSelfInitializationError;
class Quota {
 public:
  Quota(const ClassSelfDeferred& preparation, analysis::SemanticBudget budget)
      : left(budget), initial(budget) {
    failure = preparation.diagnostic.recovery.value_or(
        ClassSelfInitializationFailure{Error::kCorrespondence});
  }
  void Require(bool ok, Error kind) const {
    if (!ok) {
      auto f = failure;
      f.kind = kind;
      throw f;
    }
  }
  void Step(std::uint64_t n = 1) {
    Require(n <= left.work, Error::kWorkLimit);
    left.work -= n;
  }
  void Admit(std::size_t n, std::size_t w = 1) {
    Require(w && n <= left.bytes / w, Error::kResourceLimit);
    left.bytes -= n * w;
  }
  template <class T>
  void Reserve(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T));
    v.reserve(n);
    Admit(v.capacity() - n, sizeof(T));
  }
  template <class T, class... U>
  T Take(std::variant<T, U...> value, Error kind) {
    if (auto* t = std::get_if<T>(&value)) return std::move(*t);
    Require(false, ClassSelfResource(value).value_or(kind));
    throw failure;
  }
  void Account(ClassSelfInitializationCost c) {
    Admit(c.bytes);
    Step(c.work);
  }
  ClassSelfInitializationCost Cost() const {
    return {initial.bytes - left.bytes, initial.work - left.work};
  }
  analysis::SemanticBudget left, initial;
  ClassSelfInitializationFailure failure{Error::kCorrespondence};
};
std::vector<ClassSelfFamilyInput> Families(const ClassSelfDeferred& p,
                                           Quota& q) {
  std::vector<ClassSelfFamilyInput> result;
  q.Reserve(result, p.members.bodies.size() + 1);
  result.push_back({p.initializer_family, p.context});
  for (const auto& body : p.members.bodies) {
    q.Step();
    const auto* family = std::get_if<FamilyPreparation>(&body);
    q.Require(family, Error::kScope);
    result.push_back({family->Input(), family->Context()});
  }
  return result;
}
ClassSelfInitializationInput Input(
    const corpus::AbcFile& f, const abc::Header& h, const abc::MetadataIndex& i,
    const abc::MethodLookup& l, const analysis::MethodRoleIndex& roles,
    const analysis::ClassMethodSites& classes, const FileFunction& creator,
    const ClassSelfDeferred& p,
    std::span<const ClassSelfFamilyInput> families) {
  const auto& raw = p.initialization.Evidence();
  return {f,        h,       i, l, roles, classes, creator, raw.occurrence,
          raw.call, families};
}
struct OuterTarget {
  ClassSelfOuterBinding binding;
  SymbolId child;
};
std::vector<OuterTarget> ResolveTargets(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const FileFunction& creator, const ClassSelfDeferred& p, CaptureTree parent,
    const CaptureStorage* storage, Quota& q) {
  const auto& raw = p.initialization.Evidence();
  const auto& scope = p.scope.Evidence();
  const auto& child = p.initializer_family.members[0].function;
  const auto& graph = creator.values.graph;
  q.Require(parent.tree.identity == raw.identity &&
                parent.tree.method == raw.creator &&
                parent.flow.value_revision == graph.revision &&
                parent.flow.cfg_revision == graph.cfg_revision &&
                parent.tree.value_revision == graph.revision &&
                parent.tree.cfg_revision == graph.cfg_revision,
            Error::kOccurrence);
  const analysis::InstructionId site{
      graph.origins[graph.ops[raw.call.method.value].origin.value].site.value};
  auto creation = q.Take<analysis::CreationBinding>(
      analysis::BindCreation(file, header, index, lookup,
                             creator.input.Method(), creator.body,
                             creator.input.Proof().environments, site.value),
      Error::kOccurrence);
  q.Require(creation.target.position == p.context.Position() &&
                p.context.CreationSite() &&
                creation.creation == p.context.CreationSite()->creation,
            Error::kOccurrence);
  const CaptureFunction original{
      creator.input,
      creator.values,
      storage,
      creator.module_capture_prefix ? &*creator.module_capture_prefix : nullptr,
      nullptr,
      creator.self_order ? &*creator.self_order : nullptr};
  auto cells =
      q.Take<CaptureCells>(BindCaptureCells(index, original, child.Capture(),
                                            site, creation, q.left),
                           Error::kStorage);
  q.Admit(cells.allocated_bytes);
  q.Step(cells.work);
  q.Step(
      q.Take<std::uint64_t>(VerifyCaptureCells(index, original, child.Capture(),
                                               site, creation, cells, q.left),
                            Error::kStorage));
  q.Step(q.Take<std::uint64_t>(VerifyDeferredCaptureTree(cells, parent, q.left),
                               Error::kScope));
  const auto* source = storage                ? &storage->cells
                       : creator.values.cells ? &*creator.values.cells
                                              : nullptr;
  q.Require(source && child.Cells() && p.evaluation.cells, Error::kStorage);
  const auto& bindings = parent.flow.original && parent.flow.original->cells
                             ? parent.flow.original->cells->bindings
                             : parent.flow.cell_bindings;
  std::vector<OuterTarget> result;
  q.Reserve(result, scope.requirements.size());
  for (const auto& requirement : scope.requirements) {
    q.Step();
    if (requirement.owner.family || requirement.owner.member ||
        requirement.kind != ClassSelfRequirementKind::kOuter)
      continue;
    const auto local = FindCell(*child.Cells(), requirement.incoming);
    const auto rendered =
        FindCell(p.evaluation.cells->cells, requirement.incoming);
    q.Require(local && rendered &&
                  rendered->value < p.initializer.bindings.cells.size(),
              Error::kStorage);
    const auto at =
        std::lower_bound(cells.targets.begin(), cells.targets.end(), *local,
                         [&](const auto& target, CellId id) {
                           q.Step();
                           return target.child < id;
                         });
    q.Require(at != cells.targets.end() && at->child == *local,
              Error::kStorage);
    const auto* target = std::get_if<ParentCell>(&at->target);
    q.Require(target && target->cell.value < source->cells.size() &&
                  target->cell.value < parent.flow.cells.size() &&
                  parent.flow.cells[target->cell.value] &&
                  target->cell.value < bindings.size(),
              Error::kStorage);
    const auto symbol = *parent.flow.cells[target->cell.value];
    q.Require(symbol.value < parent.tree.symbols.size() &&
                  bindings[target->cell.value].binding == symbol,
              Error::kStorage);
    const auto& address = source->cells[target->cell.value].storage;
    const auto& resolved = bindings[target->cell.value].storage;
    q.Require(resolved.slot.value ==
                  std::visit([](const auto& value) { return value.slot.value; },
                             address),
              Error::kStorage);
    if (const auto* allocated = std::get_if<AllocatedCell>(&address)) {
      const auto* environment =
          std::get_if<analysis::EnvironmentId>(&resolved.environment);
      q.Require(
          environment && environment->value < source->allocations.size() &&
              source->allocations[environment->value] == allocated->allocation,
          Error::kStorage);
    } else {
      const auto* depth =
          std::get_if<analysis::CapturedDepth>(&resolved.environment);
      q.Require(depth && *depth == std::get<IncomingCell>(address).depth,
                Error::kStorage);
    }
    const auto& binding = parent.tree.symbols[symbol.value];
    q.Require((binding.kind == BindingKind::kLocal ||
               binding.kind == BindingKind::kCapture) &&
                  binding.name.value < parent.tree.names.size(),
              Error::kScope);
    result.push_back({{requirement.incoming,
                       requirement.domain,
                       {parent.tree.owner, symbol}},
                      p.initializer.bindings.cells[rendered->value]});
  }
  return result;
}
}  // namespace
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyDeferredClassSelf(const corpus::AbcFile& f, const abc::Header& h,
                        const abc::MetadataIndex& i, const abc::MethodLookup& l,
                        const analysis::MethodRoleIndex& roles,
                        const analysis::ClassMethodSites& classes,
                        const FileFunction& creator, const ClassSelfDeferred& p,
                        analysis::SemanticBudget budget) {
  Quota q(p, budget);
  try {
    const auto families = Families(p, q);
    const auto input = Input(f, h, i, l, roles, classes, creator, p, families);
    q.Account(q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfScope(input, p.initialization.Evidence(),
                             p.scope.Evidence(), q.left),
        Error::kCorrespondence));
    return q.Cost();
  } catch (const ClassSelfInitializationFailure& e) {
    return e;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfEnclosure(
    const corpus::AbcFile& f, const abc::Header& h, const abc::MetadataIndex& i,
    const abc::MethodLookup& l, const analysis::MethodRoleIndex& roles,
    const analysis::ClassMethodSites& classes, const FileFunction& creator,
    const ClassSelfDeferred& p, const ClassTree& layout,
    FunctionOwnerId syntax_owner, CaptureTree parent,
    const CaptureStorage* storage, analysis::SemanticBudget budget) {
  Quota q(p, budget);
  try {
    const auto families = Families(p, q);
    const auto input = Input(f, h, i, l, roles, classes, creator, p, families);
    q.Account(q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfScope(input, p.initialization.Evidence(),
                             p.scope.Evidence(), q.left),
        Error::kCorrespondence));
    const auto expected =
        ResolveTargets(f, h, i, l, creator, p, parent, storage, q);
    const auto& tree = p.initializer.tree;
    q.Require(tree.class_inner &&
                  tree.class_inner->name.value < tree.names.size() &&
                  layout.name == tree.names[tree.class_inner->name.value],
              Error::kName);
    q.Require(expected.size() == p.bindings.size() &&
                  expected.size() == tree.captures.size() &&
                  tree.capture_requirements.empty() && layout.class_self &&
                  layout.class_self->outer.size() == expected.size(),
              Error::kScope);
    for (std::size_t n = 0; n < expected.size(); ++n) {
      q.Step();
      const auto& target = expected[n];
      const auto& actual = p.bindings[n];
      q.Require(
          target.binding.storage.depth == actual.storage.depth &&
              target.binding.storage.slot.value == actual.storage.slot.value &&
              target.binding.domain == actual.domain &&
              target.binding.target == actual.target &&
              tree.captures[n].symbol == target.child &&
              tree.captures[n].target == actual.target &&
              layout.class_self->outer[n] == actual.target,
          Error::kStorage);
      q.Require(target.child.value < tree.symbols.size(), Error::kScope);
      const auto& child = tree.symbols[target.child.value];
      const auto& source = parent.tree.symbols[actual.target.symbol.value];
      q.Require(child.name.value < tree.names.size() &&
                    tree.names[child.name.value] ==
                        parent.tree.names[source.name.value],
                Error::kName);
    }
    const auto creation = p.initialization.Evidence().call.creation;
    q.Require(creation.value < parent.flow.expressions.size() &&
                  parent.flow.expressions[creation.value],
              Error::kOccurrence);
    const auto expression = *parent.flow.expressions[creation.value];
    q.Require(expression.value < parent.tree.expressions.size(),
              Error::kOccurrence);
    const auto* original = std::get_if<ClassExpr>(
        &parent.tree.expressions[expression.value].expression);
    q.Require(original && original->definition.get() == &layout &&
                  layout.operation == creation &&
                  layout.identity == creator.input.Identity(),
              Error::kOccurrence);
    q.Account(q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfClassDraft(input, p.initialization.Evidence(),
                                  p.scope.Evidence(), p.members.layout,
                                  p.evaluation, p.initializer, q.left,
                                  p.bindings, syntax_owner),
        Error::kCorrespondence));
    const auto structure = q.Take<TreeStructure>(
        VerifyTreeStructure(parent.tree, q.left), Error::kName);
    q.Admit(structure.allocated_bytes);
    q.Step(structure.work);
    return q.Cost();
  } catch (const ClassSelfInitializationFailure& e) {
    return e;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
ClassSelfInitializationDiagnostic BindDeferredClassSelf(
    const corpus::AbcFile& f, const abc::Header& h, const abc::MetadataIndex& i,
    const abc::MethodLookup& l, const analysis::MethodRoleIndex& roles,
    const analysis::ClassMethodSites& classes, const FileFunction& creator,
    ClassSelfDeferred& p, ClassTree& layout, FunctionOwnerId syntax_owner,
    CaptureTree parent, const CaptureStorage* storage,
    analysis::SemanticBudget budget, const ModuleEnclosure* module_enclosure) {
  auto result = p.diagnostic;
  Quota q(p, budget);
  q.failure.projection.reset();
  try {
    const auto families = Families(p, q);
    const auto input = Input(f, h, i, l, roles, classes, creator, p, families);
    q.Account(q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfScope(input, p.initialization.Evidence(),
                             p.scope.Evidence(), q.left),
        Error::kCorrespondence));
    auto targets = ResolveTargets(f, h, i, l, creator, p, parent, storage, q);
    auto& tree = p.initializer.tree;
    q.Require(syntax_owner != parent.tree.owner, Error::kScope);
    const auto previous_owner = tree.owner;
    tree.owner = syntax_owner;
    for (auto& expression : tree.expressions) {
      q.Step();
      if (auto* receiver = std::get_if<ThisExpr>(&expression.expression)) {
        q.Require(receiver->owner == previous_owner, Error::kScope);
        receiver->owner = syntax_owner;
      }
    }
    // Bind references, never values: a still-uninitialized outer declaration
    // must throw natively when this initializer reads it.
    q.Require(tree.capture_requirements.size() == targets.size() &&
                  tree.captures.empty(),
              Error::kScope);
    q.Reserve(tree.captures, targets.size());
    q.Reserve(p.bindings, targets.size());
    q.Require(targets.size() <= UINT32_MAX - tree.names.size(),
              Error::kResourceLimit);
    const auto name_count = tree.names.size() + targets.size();
    q.Admit(name_count, sizeof(std::u16string));
    tree.names.reserve(name_count);
    q.Admit(tree.names.capacity() - name_count, sizeof(std::u16string));
    for (const auto& target : targets) {
      q.Step();
      q.Require(target.child.value < tree.symbols.size(), Error::kScope);
      auto& binding = tree.symbols[target.child.value];
      q.Require(binding.kind == BindingKind::kCaptureRequirement,
                Error::kScope);
      const auto& source =
          parent.tree.symbols[target.binding.target.symbol.value];
      const auto& name = parent.tree.names[source.name.value];
      q.Admit(name.size() + 1, sizeof(char16_t));
      const NameId id{static_cast<std::uint32_t>(tree.names.size())};
      tree.names.push_back(name);
      q.Admit(tree.names.back().capacity() - name.size(), sizeof(char16_t));
      binding.name = id;
      binding.evidence = source.evidence;
      binding.kind = BindingKind::kCapture;
      tree.captures.push_back({target.child, target.binding.target});
      p.bindings.push_back(target.binding);
    }
    tree.capture_requirements.clear();
    if (module_enclosure && !tree.modules.empty()) {
      const auto& member = p.initializer_family.members[0].function.Normal();
      q.Require(
          module_enclosure->Identity() == tree.identity &&
              module_enclosure->Context().record() == member.input.Record(),
          Error::kScope);
      auto aliases = q.Take<ModuleAliases>(
          BindModuleAliases(f, h, i, member.input, member.values,
                            module_enclosure->Context(),
                            module_enclosure->Owner(), {}, q.left),
          Error::kScope);
      q.Admit(aliases.allocated_bytes);
      q.Step(aliases.work);
      q.Require(aliases.evaluations.size() == p.evaluation.modules.size() &&
                    aliases.aliases.size() == tree.modules.size(),
                Error::kScope);
      for (std::size_t n = 0; n < aliases.evaluations.size(); ++n) {
        q.Step();
        auto& old = p.evaluation.modules[n];
        const auto& actual = aliases.evaluations[n];
        q.Require(old.pair == actual.pair && old.read == actual.read &&
                      old.target.symbol == actual.target.symbol,
                  Error::kScope);
        old.target = actual.target;
      }
      for (auto& alias : tree.modules) {
        q.Step();
        alias.target.owner = module_enclosure->Owner();
      }
      q.Step(q.Take<std::uint64_t>(
          VerifyModuleAliasTree(aliases, p.evaluation, tree, q.left),
          Error::kScope));
    }

    q.Require(
        p.members.layout.class_self &&
            layout.operation == p.initialization.Evidence().call.creation &&
            layout.identity == creator.input.Identity(),
        Error::kOccurrence);
    // The existing structure traversal checks visibility and shadowing at the
    // real ClassExpr, in one pass, including declarations that are still TDZ.
    ClassTreeInitialization initialization{
        {},
        p.scope.Evidence().binding,
        {},
        {},
        p.scope.Evidence().initializer_exception,
        p.members.layout.class_self->first_owner,
        p.members.layout.class_self->owner_end,
        {}};
    q.Reserve(initialization.ports, p.scope.Evidence().ports.size());
    initialization.ports.insert(initialization.ports.end(),
                                p.scope.Evidence().ports.begin(),
                                p.scope.Evidence().ports.end());
    q.Reserve(initialization.cleanup, p.scope.Evidence().cleanup.size());
    initialization.cleanup.insert(initialization.cleanup.end(),
                                  p.scope.Evidence().cleanup.begin(),
                                  p.scope.Evidence().cleanup.end());
    q.Reserve(initialization.outer, p.bindings.size());
    for (const auto& binding : p.bindings)
      initialization.outer.push_back(binding.target);
    layout.class_self.emplace(std::move(initialization));
    const auto verified = q.Take<ClassSelfInitializationCost>(
        VerifyClassSelfEnclosure(f, h, i, l, roles, classes, creator, p, layout,
                                 syntax_owner, parent, storage, q.left),
        Error::kCorrespondence);
    q.Account(verified);
    result.bound_outer = static_cast<std::uint32_t>(p.bindings.size());
    // All raw self reads must resolve to the inner binding, including methods
    // invoked re-entrantly during static initialization. An ordinary creator
    // cache is initialized only after native ClassDefinitionEvaluation ends.
    // This initializer-only projection cannot certify those member references.
    for (const auto& reference : p.scope.Evidence().references) {
      q.Step();
      if (reference.owner.family || reference.owner.member) {
        q.failure.projection =
            ClassSelfInitializationFailure::Projection::kFamilyEnclosure;
        const auto& family = input.families[reference.owner.family]
                                 .family.members[reference.owner.member]
                                 .function;
        q.failure.location =
            OperationLocation(family.Graph(), reference.guard, family.Method());
        q.Require(false, Error::kScope);
      }
    }
    for (const auto& expression : tree.expressions) {
      q.Step();
      if (std::holds_alternative<FunctionExpr>(expression.expression)) {
        q.failure.projection =
            ClassSelfInitializationFailure::Projection::kFamilyEnclosure;
        q.Require(false, Error::kScope);
      }
    }
    tree.class_scope = q.Take<std::shared_ptr<const ClassInnerScopeProof>>(
        ClassInnerScopeProof::Build(input, p.initialization.Evidence(), q.left),
        Error::kCorrespondence);
    q.Account(tree.class_scope->Cost());
    layout.class_self->certificate = tree.class_scope;
    // Do not equate a native class's whole-expression Commit with the raw
    // self-store / initializer Before / initializer Commit / public-store
    // ports. The common initializer semantics now verifies the sealed inner
    // scope; creator recovery must still match the distinct publication ports.
    q.failure.projection =
        ClassSelfInitializationFailure::Projection::kClassInitializationPorts;
    q.failure.location = OperationLocation(
        creator.values.graph, p.initialization.Evidence().call.call,
        creator.input.Method());
    const auto& graph = p.initializer_family.members[0].function.Graph();
    auto constants = q.Take<TreeConstants>(
        ResolveTreeConstants(f, h, i, p.context.Target(), graph, q.left),
        Error::kCorrespondence);
    q.Admit(constants.allocated_bytes);
    q.Step(constants.work);
    auto control = q.Take<ControlPlan>(BuildControlPlan(graph, q.left),
                                       Error::kCorrespondence);
    q.Admit(control.allocated_bytes);
    q.Step(control.work);
    auto names = constants.View();
    names.name_count = tree.names.size();
    names.text_count = tree.texts.size();
    auto semantics = q.Take<TreeSemantics>(
        VerifyTreeSemantics(graph, control, p.evaluation, tree, names, q.left),
        Error::kCorrespondence);
    q.Admit(semantics.allocated_bytes);
    q.Step(semantics.work);
    p.flow.emplace(std::move(semantics.flow));
    result.recovery->projection =
        ClassSelfInitializationFailure::Projection::kClassInitializationPorts;
    result.recovery->location = OperationLocation(
        creator.values.graph, p.initialization.Evidence().call.call,
        creator.input.Method());
    p.enclosure_cost = q.Cost();
    return result;
  } catch (const ClassSelfInitializationFailure& e) {
    result.recovery = e;
    if (e.kind != Error::kResourceLimit && e.kind != Error::kWorkLimit) {
      result.recovery->kind = Error::kRecoveryGate;
      result.recovery->projection = e.projection.value_or(
          e.kind == Error::kName
              ? ClassSelfInitializationFailure::Projection::kNameResolution
              : ClassSelfInitializationFailure::Projection::kCaptureEnclosure);
    }
  } catch (const std::bad_alloc&) {
    result.recovery = q.failure;
    result.recovery->kind = Error::kResourceLimit;
  } catch (const std::length_error&) {
    result.recovery = q.failure;
    result.recovery->kind = Error::kResourceLimit;
  }
  p.enclosure_cost = q.Cost();
  return result;
}
}  // namespace abcd::source
