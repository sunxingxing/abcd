#include "class_self_initialization_file.h"

#include <new>
#include <stdexcept>

#include "class_self_deferred.h"
#include "class_self_resource.h"
#include "class_self_scope.h"
#include "class_tree.h"
namespace abcd::source {
namespace {
template <class T>
struct DeferredAllocator {
  using value_type = T;
  analysis::SemanticBudget* budget;
  template <class U>
  DeferredAllocator(const DeferredAllocator<U>& other) : budget(other.budget) {}
  explicit DeferredAllocator(analysis::SemanticBudget& left) : budget(&left) {}
  T* allocate(std::size_t n) {
    if (n > budget->bytes / sizeof(T)) throw std::bad_alloc{};
    budget->bytes -= n * sizeof(T);
    return std::allocator<T>{}.allocate(n);
  }
  void deallocate(T* p, std::size_t n) { std::allocator<T>{}.deallocate(p, n); }
  template <class U>
  bool operator==(const DeferredAllocator<U>& other) const {
    return budget == other.budget;
  }
};
}  // namespace
static ClassSelfInitializationDiagnostic PrepareClassSelfInitialization(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions,
    const analysis::ClassMethodSites& classes, const FileFunction& parent,
    const ClassInitializerCall& call, CreatorClassOccurrence occurrence,
    analysis::SemanticBudget budget, const RecordModuleContext* modules,
    FileFunction* consumer) {
  const auto initial = budget;
  using Error = ClassSelfInitializationError;
  ClassSelfInitializationDiagnostic result;
  result.creator = parent.input.Method();
  result.creation = call.creation;
  ClassSelfInitializationFailure failure{Error::kOccurrence};
  failure.creator = result.creator;
  failure.creation = call.creation;
  failure.location =
      OperationLocation(parent.values.graph, call.call, result.creator);
  const auto require = [&](bool ok, Error error) {
    if (!ok) {
      failure.kind = error;
      throw failure;
    }
  };
  const auto admit = [&](std::size_t n, std::size_t width = 1) {
    require(width && n <= budget.bytes / width, Error::kResourceLimit);
    budget.bytes -= n * width;
  };
  const auto step = [&](std::uint64_t n) {
    require(n <= budget.work, Error::kWorkLimit);
    budget.work -= n;
  };
  try {
    admit(sizeof(result) + sizeof(failure));
    // Conditional evidence is read only AFTER retaining the original Closed
    // refusal. No retry or fall-through can admit a source plan from this path.
    auto loaded = ResolveClassInitializerContext(
        file, header, index, lookup, roles, functions, parent, call, budget);
    const auto* initializer = std::get_if<FunctionContext>(&loaded);
    require(initializer,
            ClassSelfResource(loaded).value_or(Error::kOccurrence));
    admit(initializer->AllocatedBytes());
    step(initializer->Work());
    result.initializer = initializer->Target();
    failure.initializer = result.initializer;
    const auto& graph = parent.values.graph;
    const auto site =
        graph.origins[graph.ops[call.method.value].origin.value].site;
    const ParentModuleInput input_modules{parent.input, {site.value}};
    auto read = ReadFunctionFamily(
        file, header, index, lookup, roles, functions, initializer->Position(),
        parent.input.Identity(), budget, modules, initializer, {},
        FamilyClassMode::kReject, &input_modules, {}, nullptr, &classes,
        nullptr, ClassRootBinding::kRequirements);
    const auto* family = std::get_if<FileFamily>(&read);
    require(family, ClassSelfResource(read).value_or(Error::kScope));
    admit(family->allocated_bytes);
    step(family->work);
    std::vector<ClassDefinitionSite> definitions;
    admit(parent.values.definitions.sites.size(), sizeof(ClassDefinitionSite));
    definitions.reserve(parent.values.definitions.sites.size());
    admit(definitions.capacity() - parent.values.definitions.sites.size(),
          sizeof(ClassDefinitionSite));
    for (const auto& definition : parent.values.definitions.sites) {
      step(1);
      if (definition.creation == call.creation)
        definitions.push_back(definition);
    }
    // Member families are read for complete transitive WRITE evidence. This is
    // not admission of mixed accessor/static-field source placement.
    auto prepared = PrepareClassTree(
        file, header, index, lookup, roles, functions, classes, parent,
        call.creation, parent.input.Identity(), nullptr, budget, nullptr,
        definitions, {static_cast<std::uint32_t>(family->members.size())},
        RecoveryMode::kCompleteOnly, nullptr, ClassRootBinding::kRequirements);
    auto* members = std::get_if<ClassPreparation>(&prepared);
    require(members, ClassSelfResource(prepared).value_or(Error::kScope));
    admit(members->allocated_bytes);
    step(members->work);
    std::vector<ClassSelfFamilyInput> families;
    admit(1 + members->bodies.size(), sizeof(ClassSelfFamilyInput));
    families.reserve(1 + members->bodies.size());
    admit(families.capacity() - (1 + members->bodies.size()),
          sizeof(ClassSelfFamilyInput));
    families.push_back({*family, *initializer});
    for (const auto& body : members->bodies) {
      step(1);
      const auto* member = std::get_if<FamilyPreparation>(&body);
      require(member, Error::kWrite);
      families.push_back({member->Input(), member->Context()});
    }
    const ClassSelfInitializationInput input{file,  header,  index,  lookup,
                                             roles, classes, parent, occurrence,
                                             call,  families};
    auto derived = ClassSelfInitializationProof::Build(input, budget);
    if (const auto* error =
            std::get_if<ClassSelfInitializationFailure>(&derived))
      throw *error;
    const auto& proof = std::get<ClassSelfInitializationProof>(derived);
    admit(proof.Cost().bytes);
    step(proof.Cost().work);
    auto verified =
        VerifyClassSelfInitialization(input, proof.Evidence(), budget);
    if (const auto* error =
            std::get_if<ClassSelfInitializationFailure>(&verified))
      throw *error;
    const auto verified_cost = std::get<ClassSelfInitializationCost>(verified);
    admit(verified_cost.bytes);
    step(verified_cost.work);
    const auto& evidence = proof.Evidence();
    require(evidence.reads.size() <= UINT32_MAX &&
                evidence.captures.size() <= UINT32_MAX &&
                evidence.cleanup.size() <= UINT32_MAX &&
                families.size() <= UINT32_MAX,
            Error::kResourceLimit);
    result.status = ClassSelfInitializationStatus::kVerified;
    result.allocation = evidence.allocation;
    result.store = evidence.store;
    result.slot = evidence.slot;
    result.reads = static_cast<std::uint32_t>(evidence.reads.size());
    result.captures = static_cast<std::uint32_t>(evidence.captures.size());
    result.cleanup = static_cast<std::uint32_t>(evidence.cleanup.size());
    result.families = static_cast<std::uint32_t>(families.size());
    // Retain the original Closed refusal. The scope view is a fresh consumer
    // of the raw seal, not permission to retry the reader or fabricate a tree.
    ClassSelfScopeDiagnostic scope;
    try {
      auto scoped = ClassSelfScopeProof::Build(input, evidence, budget);
      if (const auto* error =
              std::get_if<ClassSelfInitializationFailure>(&scoped)) {
        scope.failure = *error;
      } else {
        const auto& view = std::get<ClassSelfScopeProof>(scoped);
        admit(view.Cost().bytes);
        step(view.Cost().work);
        const auto checked =
            VerifyClassSelfScope(input, evidence, view.Evidence(), budget);
        if (const auto* error =
                std::get_if<ClassSelfInitializationFailure>(&checked)) {
          scope.failure = *error;
        } else {
          const auto checked_cost =
              std::get<ClassSelfInitializationCost>(checked);
          admit(checked_cost.bytes);
          step(checked_cost.work);
          const auto& prepared = view.Evidence();
          scope.status = ClassSelfInitializationStatus::kVerified;
          scope.owners = static_cast<std::uint32_t>(prepared.owners.size());
          scope.references =
              static_cast<std::uint32_t>(prepared.references.size());
          for (const auto& requirement : prepared.requirements) {
            step(1);
            if (requirement.kind == ClassSelfRequirementKind::kClassInner)
              ++scope.inner;
            else
              ++scope.outer;
          }
          for (const auto& port : prepared.ports) {
            step(1);
            scope.public_commits +=
                port.kind == ClassSelfPortKind::kPublicCommit;
          }
          if (consumer) {
            auto built = BuildClassSelfClassDraft(input, evidence, prepared,
                                                  members->layout, *consumer,
                                                  budget, modules);
            if (const auto* error =
                    std::get_if<ClassSelfInitializationFailure>(&built)) {
              auto recovery = *error;
              if (recovery.kind != Error::kResourceLimit &&
                  recovery.kind != Error::kWorkLimit) {
                recovery.projection =
                    recovery.kind == Error::kName
                        ? ClassSelfInitializationFailure::Projection::
                              kNameResolution
                    : recovery.kind == Error::kGuard
                        ? ClassSelfInitializationFailure::Projection::kGuard
                        : ClassSelfInitializationFailure::Projection::kSyntax;
                recovery.kind = Error::kRecoveryGate;
              }
              result.recovery = recovery;
            } else {
              auto& draft = std::get<ClassSelfClassDraft>(built);
              admit(draft.allocated_bytes);
              step(draft.work);
              consumer->self_order.emplace(std::move(draft.order));
              const auto& tree = draft.initializer.tree;
              result.syntax_owners =
                  static_cast<std::uint32_t>(members->next_owner);
              for (const auto& expression : tree.expressions) {
                step(1);
                const auto* ref =
                    std::get_if<SymbolExpr>(&expression.expression);
                if (ref && tree.symbols[ref->symbol.value].kind ==
                               BindingKind::kClassInner)
                  ++result.tree_references;
              }
              ClassSelfInitializationFailure recovery{Error::kRecoveryGate};
              recovery.creator = evidence.creator;
              recovery.initializer = evidence.initializer;
              recovery.creation = evidence.call.creation;
              recovery.allocation = evidence.allocation;
              recovery.store = evidence.store;
              recovery.slot = evidence.slot;
              recovery.location = OperationLocation(
                  parent.values.graph, evidence.call.call, evidence.creator);
              for (const auto& reference : prepared.references) {
                step(1);
                if (reference.owner.family || reference.owner.member) continue;
                const auto& function = family->members[0].function;
                recovery.read = reference.read;
                recovery.guard = reference.guard;
                recovery.location = OperationLocation(
                    function.Graph(), reference.guard, function.Method());
                break;
              }
              recovery.projection =
                  tree.capture_requirements.empty()
                      ? ClassSelfInitializationFailure::Projection::
                            kClassInitializationPorts
                      : ClassSelfInitializationFailure::Projection::
                            kCaptureEnclosure;
              result.recovery = recovery;
              // A retained class is available after StaticInitialize even
              // when a different value (e.g. a method result) is exported. The
              // creator's joint recovery must still consume every such port.
              bool completed = false;
              for (const auto& port : prepared.ports) {
                step(1);
                completed |= port.kind == ClassSelfPortKind::kCompletedClass;
              }
              result.scope_view = scope;
              if ((scope.public_commits || completed) && !consumer->class_self) {
                consumer->class_self = std::allocate_shared<ClassSelfDeferred>(
                    DeferredAllocator<ClassSelfDeferred>{budget},
                    std::move(std::get<FunctionContext>(loaded)),
                    std::move(std::get<FileFamily>(read)), std::move(*members),
                    std::move(std::get<ClassSelfInitializationProof>(derived)),
                    std::move(std::get<ClassSelfScopeProof>(scoped)),
                    std::move(draft.initializer), std::move(draft.evaluation),
                    result);
                consumer->class_self->allocated_bytes =
                    initial.bytes - budget.bytes;
                consumer->class_self->work = initial.work - budget.work;
              }
            }
          }
        }
      }
    } catch (const ClassSelfInitializationFailure& error) {
      scope.status = ClassSelfInitializationStatus::kMissing;
      scope.failure = error;
    }
    result.scope_view = scope;
    return result;
  } catch (const ClassSelfInitializationFailure& error) {
    result.failure = error;
    return result;
  } catch (const std::bad_alloc&) {
    failure.kind = Error::kResourceLimit;
    result.failure = failure;
    return result;
  } catch (const std::length_error&) {
    failure.kind = Error::kResourceLimit;
    result.failure = failure;
    return result;
  }
}
ClassSelfInitializationDiagnostic DiagnoseClassSelfInitialization(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions,
    const analysis::ClassMethodSites& classes, const FileFunction& parent,
    const ClassInitializerCall& call, CreatorClassOccurrence occurrence,
    analysis::SemanticBudget budget, const RecordModuleContext* modules) {
  return PrepareClassSelfInitialization(file, header, index, lookup, roles,
                                        functions, classes, parent, call,
                                        occurrence, budget, modules, nullptr);
}
ClassSelfInitializationDiagnostic ConsumeClassSelfInitialization(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions,
    const analysis::ClassMethodSites& classes, FileFunction& parent,
    const ClassInitializerCall& call, CreatorClassOccurrence occurrence,
    analysis::SemanticBudget budget, const RecordModuleContext* modules) {
  return PrepareClassSelfInitialization(file, header, index, lookup, roles,
                                        functions, classes, parent, call,
                                        occurrence, budget, modules, &parent);
}
}  // namespace abcd::source
