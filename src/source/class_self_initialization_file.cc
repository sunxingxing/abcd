#include "class_self_initialization_file.h"

#include <new>
#include <stdexcept>

#include "class_self_resource.h"
#include "class_self_scope.h"
#include "class_tree.h"
namespace abcd::source {
ClassSelfInitializationDiagnostic DiagnoseClassSelfInitialization(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions,
    const analysis::ClassMethodSites& classes, const FileFunction& parent,
    const ClassInitializerCall& call, CreatorClassOccurrence occurrence,
    analysis::SemanticBudget budget, const RecordModuleContext* modules) {
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
    const auto* members = std::get_if<ClassPreparation>(&prepared);
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
}  // namespace abcd::source
