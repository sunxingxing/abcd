#include "class_tree.h"
#include <bit>
#include <algorithm>
#include <new>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include "runtime_name.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/global_reference.h"
namespace abcd::source {
std::variant<ClassParentScope, ClassTreeError> CheckClassParentScope(
    const ClassTree& tree, std::uint64_t work) {
  const auto initial = work;
  const auto step = [&](std::uint64_t count = 1) {
    if (count > work) throw ClassTreeError::kWorkLimit;
    work -= count;
  };
  try {
    step(); step(tree.member_sites.size()); step(tree.members.size()); step(tree.definitions.size());
    if (tree.initializer_site || !tree.definitions.empty() || !tree.BodiesPrepared())
      return ClassParentScope{false, initial - work};
    const auto independent = [&](const PreparedFamily& prepared) {
      step(); const auto& family = prepared.Family();
      if ((family.root_capture && !family.root_capture->targets.empty()) ||
          !family.members[family.root.value].tree.captures.empty()) return false;
      for (const auto& member : family.members) {
        step(); if (!member.tree.modules.empty()) return false;
      }
      return true;
    };
    if (!independent(*tree.constructor)) return ClassParentScope{false, initial - work};
    for (const auto& member : tree.members)
      if (!independent(*member)) return ClassParentScope{false, initial - work};
    return ClassParentScope{true, initial - work};
  } catch (ClassTreeError error) { return error; }
}

namespace {
using Error = ClassTreeError;
template<class T, class... U> T Take(std::variant<U...> value) {
  if (auto* result = std::get_if<T>(&value)) return std::move(*result);
  std::visit([](const auto& error) {
    if constexpr (!std::is_same_v<T, std::decay_t<decltype(error)>>) throw ClassTreeFailure{error};
  }, value);
  throw ClassTreeFailure{Error::kCreation};
}
}
bool ClassTree::BodiesPrepared() const {
  if (!omissions.empty()) return false;
  for (const auto& definition : definitions)
    for (const auto& omitted : definition.omissions) if (omitted) return false;
  return DraftPrepared();
}
bool ClassTree::DraftPrepared() const {
  if (!constructor || members.size() != member_sites.size() ||
      initializer.has_value() != initializer_site.has_value()) return false;
  std::size_t gap = 0;
  for (std::size_t i = 0; i < members.size(); ++i) {
    if (members[i]) continue;
    if (gap >= omissions.size()) return false;
    const auto& omitted = omissions[gap++];
    const auto& context = omitted.context;
    if (context.Constructor() || context.DefinedMember() ||
        context.Identity() != identity || context.Occurrence() != member_sites[i] ||
        context.Site().property != i || context.Site().creator != creation.creator || context.Site().pc != creation.pc ||
        !omitted.InputGap()) return false;
  }
  if (gap != omissions.size()) return false;
  for (const auto& definition : definitions) {
    const auto methods = definition.site.Methods();
    if (definition.site.creation != operation) return false;
    for (std::size_t i = 0; i < methods.size(); ++i) {
      const auto& omitted = definition.omissions[i];
      if (definition.bodies[i] && omitted) return false;
      if ((definition.bodies[i].has_value() || omitted.has_value()) != methods[i].operation.has_value()) return false;
      if (!omitted) continue;
      const auto& context = omitted->context;
      const auto* site = context.DefinedMember();
      if (!site || context.Identity() != identity || site->function.creator != creation.creator ||
          site->definition != definition.site.definition || site->kind != methods[i].kind ||
          site->placement != (definition.site.prototype ? abc::ClassPlacement::kPrototype : abc::ClassPlacement::kStatic) ||
          !omitted->InputGap()) return false;
    }
  }
  return true;
}
std::variant<ClassTree, ClassTreeFailure> BuildClassLayout(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::ClassMethodSites& classes, const FileFunction& creator,
    analysis::OpId operation, RouteInputIdentity identity, analysis::SemanticBudget budget,
    const ClassInitializerCall* initializer, std::span<const ClassDefinitionSite> definitions) {
  const auto initial = budget;
  try {
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > budget.bytes / width) throw ClassTreeFailure{Error::kResourceLimit};
      budget.bytes -= count * width;
    };
    const auto step = [&](std::uint64_t count) {
      if (count > budget.work) throw ClassTreeFailure{Error::kWorkLimit};
      budget.work -= count;
    };
    admit(1, sizeof(ClassTree));
    const auto context = Take<ClassTreeContext>(ClassTreeContext::ResolveConstructor(
        file, header, index, lookup, roles, creator, operation, identity, budget));
    admit(context.AllocatedBytes(), 1); step(context.Work());
    if (!context.Constructor()) return ClassTreeFailure{Error::kCreation};
    const auto site = *context.Constructor();
    const auto resolved = Take<abc::PoolReference>(abc::ResolvePoolReference(file, header, index,
        site.creator, site.creation.literal, abc::PoolEntityKind::kLiteralArray));
    const auto literal = Take<abc::LiteralArray>(abc::ReadLiteralArray(file, header, resolved.offset, {budget.bytes}));
    admit(literal.entries.capacity(), sizeof(abc::LiteralEntry)); step(literal.entries.size());
    const auto buffer = Take<abc::ClassBuffer>(abc::DecodeClassBuffer(literal, {budget.bytes}));
    admit(buffer.properties.capacity(), sizeof(abc::ClassProperty));
    const auto name = RuntimeName(index.methods[context.Position().value]);
    if (!name) return ClassTreeFailure{Error::kName};
    ClassTree tree;
    tree.identity = identity; tree.creation = site; tree.operation = operation; tree.callable = context.Callable();
    admit(name->size() + 1, sizeof(char16_t)); step(name->size());
    tree.name.assign(name->begin(), name->end());
    admit(tree.name.capacity() - tree.name.size(), sizeof(char16_t));
    admit(buffer.properties.size(), sizeof(analysis::ClassSiteId));
    tree.member_sites.reserve(buffer.properties.size());
    admit(tree.member_sites.capacity() - buffer.properties.size(), sizeof(analysis::ClassSiteId));
    for (std::size_t i = 0; i < buffer.properties.size(); ++i) {
      step(1); const auto& property = buffer.properties[i];
      const auto* method = std::get_if<abc::ClassMethod>(&property.value);
      if (!method || i > UINT32_MAX) return ClassTreeFailure{Error::kMember};
      // Exact creation lookup, never scan every use of a shared method per key.
      step(std::bit_width(classes.AllocatedBytes() / sizeof(analysis::ClassMethodSite)) + 1);
      const auto occurrence = classes.FindOccurrence(method->target, site.creator, site.pc, static_cast<std::uint32_t>(i));
      if (!occurrence) return ClassTreeFailure{Error::kMember};
      const auto* actual = classes.At(*occurrence);
      if (!actual || actual->literal != resolved.offset || actual->kind != method->kind ||
          actual->length != method->length || actual->placement != property.placement)
        return ClassTreeFailure{Error::kMember};
      tree.member_sites.push_back(*occurrence);
    }
    // Layout retains actual wire order. Projection/movement is a separate
    // parent-tree obligation; a metadata inventory grants no executable body.
    admit(definitions.size(), sizeof(ClassDefinitionTree));
    tree.definitions.reserve(definitions.size());
    admit(tree.definitions.capacity() - definitions.size(), sizeof(ClassDefinitionTree));
    for (const auto& definition : definitions) {
      step(1);
      if (definition.creation != operation || initializer)
        return ClassTreeFailure{Error::kCreation};
      tree.definitions.push_back({definition});
    }
    std::sort(tree.definitions.begin(), tree.definitions.end(), [&](const auto& a, const auto& b) {
      step(1); return a.site.order < b.site.order;
    });
    for (std::size_t i = 1; i < tree.definitions.size(); ++i) {
      step(1); if (tree.definitions[i - 1].site.order == tree.definitions[i].site.order)
        return ClassTreeFailure{Error::kCreation};
    }
    if (initializer) {
      if (initializer->creation != operation) return ClassTreeFailure{Error::kCreation};
      tree.initializer_site = *initializer;
    }
    if (file.CheckUnchanged()) return ClassTreeFailure{abc::MetadataError::kFileChanged};
    tree.allocated_bytes = initial.bytes - budget.bytes; tree.work = initial.work - budget.work;
    return tree;
  } catch (const ClassTreeFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return ClassTreeFailure{Error::kResourceLimit}; }
    catch (const std::length_error&) { return ClassTreeFailure{Error::kResourceLimit}; }
}
std::variant<ClassTree, ClassTreeFailure> BuildClassTree(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions, const analysis::ClassMethodSites& classes,
    const FileFunction& creator, analysis::OpId operation, RouteInputIdentity identity,
    const GlobalScope* globals, analysis::SemanticBudget budget, const ClassInitializerCall* initializer,
    std::span<const ClassDefinitionSite> definitions, RecoveryMode recovery, FunctionOwnerId first_owner,
    const CaptureTree* parent) {
  const auto initial = budget;
  try {
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > budget.bytes / width) throw ClassTreeFailure{Error::kResourceLimit};
      budget.bytes -= count * width;
    };
    const auto step = [&](std::uint64_t count) {
      if (count > budget.work) throw ClassTreeFailure{Error::kWorkLimit};
      budget.work -= count;
    };
    auto tree = Take<ClassTree>(BuildClassLayout(file, header, index, lookup, roles, classes,
        creator, operation, identity, budget, initializer, definitions));
    admit(tree.allocated_bytes, 1); step(tree.work);
    std::uint64_t next_owner = first_owner.value;
    std::optional<ClassCaptureParent> source_parent;
    if (parent) source_parent.emplace(ClassCaptureParent{*parent, operation});
    const auto destination = [&]() -> FamilyDestination {
      if (next_owner > std::numeric_limits<std::uint32_t>::max())
        throw ClassTreeFailure{Error::kResourceLimit};
      return {Strictness::kStrict, globals, FunctionFragment::kClassMember,
          {static_cast<std::uint32_t>(next_owner)}, source_parent ? &*source_parent : nullptr};
    };
    const ParentModuleInput modules{creator.input,
        {creator.values.graph.origins[creator.values.graph.ops[operation.value].origin.value].site.value}};
    tree.constructor = Take<PreparedFamily>(PreparedFamily::PrepareConstructor(file, header, index, lookup,
        roles, functions, creator, operation, identity, destination(), budget, recovery));
    admit(tree.constructor->AllocatedBytes(), 1); step(tree.constructor->Work());
    next_owner += tree.constructor->Family().members.size();
    const auto omission = [&](const auto& prepared, const auto& resolve) -> std::optional<ClassMemberOmission> {
      const auto* failed = std::get_if<FamilyPrepareFailure>(&prepared);
      if (!failed || recovery != RecoveryMode::kAllowPartial) return {};
      const auto* family = std::get_if<FamilyFileFailure>(&failed->cause);
      if (!family || !family->root || !family->method ||
          family->parent.has_value() != family->creation_pc.has_value()) return {};
      const auto* phase = std::get_if<PhasedFileFailure>(&family->cause);
      const auto* input = phase ? std::get_if<PhasedInputFailure>(phase) : nullptr;
      const auto* normal = std::get_if<FunctionFileFailure>(&family->cause);
      const auto* normal_input = normal ? std::get_if<FunctionInputFailure>(normal) : nullptr;
      const auto* proof = normal_input ? std::get_if<analysis::SemanticProofFailure>(&normal_input->cause) : nullptr;
      const bool phased_gap = input && input->cause.kind == analysis::ExceptionInputError::kInternalValue &&
          input->cause.node && input->cause.value;
      const bool normal_gap = proof && proof->kind == analysis::SemanticProofError::kInternalValue;
      if (!phased_gap && !normal_gap) return {};
      // The reader stamps the requested root before traversing validated
      // creation edges. Keep the entire member absent, never splice an unknown
      // child into a prepared family. Other failures cannot become omissions.
      admit(phased_gap ? input->allocated_bytes : proof->allocated_bytes, 1);
      step(phased_gap ? input->work : proof->work_units);
      auto context = resolve();
      admit(context.AllocatedBytes(), 1); step(context.Work());
      ClassMemberOmission gap{std::move(context), phased_gap ? decltype(ClassMemberOmission::failure){input->cause} :
          decltype(ClassMemberOmission::failure){*proof}, *family->root, *family->method, family->parent, family->creation_pc};
      if (!gap.InputGap()) throw ClassTreeFailure{Error::kMember};
      return gap;
    };
    admit(tree.member_sites.size(), sizeof(std::optional<PreparedFamily>)); tree.members.reserve(tree.member_sites.size());
    admit(tree.members.capacity() - tree.member_sites.size(), sizeof(std::optional<PreparedFamily>));
    for (const auto occurrence : tree.member_sites) {
      step(1);
      auto prepared = PreparedFamily::PrepareClass(file, header, index, lookup, roles,
          functions, classes, occurrence, identity, destination(), budget, recovery, &modules, &creator);
      if (auto gap = omission(prepared, [&] {
            return Take<ClassTreeContext>(ClassTreeContext::Resolve(file, header, index, lookup,
                roles, classes, occurrence, identity, budget));
          })) {
        if (tree.omissions.size() == tree.omissions.capacity()) {
          const auto old = tree.omissions.capacity();
          const auto capacity = old ? old * 2 : 1;
          if (capacity < old || capacity > budget.bytes / sizeof(ClassMemberOmission))
            throw ClassTreeFailure{Error::kResourceLimit};
          tree.omissions.reserve(capacity);
          admit(tree.omissions.capacity() - old, sizeof(ClassMemberOmission));
        }
        tree.omissions.push_back(std::move(*gap));
        tree.members.emplace_back();
        continue;
      }
      auto member = Take<PreparedFamily>(std::move(prepared));
      admit(member.AllocatedBytes(), 1); step(member.Work());
      next_owner += member.Family().members.size();
      tree.members.push_back(std::move(member));
    }
    for (auto& definition : tree.definitions) {
      step(1);
      const auto methods = definition.site.Methods();
      for (std::size_t i = 0; i < methods.size(); ++i) {
        if (!methods[i].operation) continue;
        auto prepared = PreparedFamily::PrepareDefinedMember(file, header, index, lookup, roles,
            functions, creator, definition.site.definition, methods[i].kind, identity, destination(), budget, recovery);
        if (auto gap = omission(prepared, [&] {
              return Take<ClassTreeContext>(ClassTreeContext::ResolveDefinedMember(file, header, index, lookup,
                  roles, functions, creator, definition.site.definition, methods[i].kind, identity, budget));
            })) {
          definition.omissions[i] = std::move(*gap);
          continue;
        }
        auto member = Take<PreparedFamily>(std::move(prepared));
        admit(member.AllocatedBytes(), 1); step(member.Work());
        next_owner += member.Family().members.size();
        definition.bodies[i] = std::move(member);
      }
    }
    std::optional<ClassStaticFields> fields;
    if (initializer) {
      if (initializer->creation != operation) return ClassTreeFailure{Error::kCreation};
      auto body = Take<ClassInitializerFunction>(ReadClassInitializerFunction(file, header, index, lookup,
          roles, functions, creator, *initializer, budget,
          globals && globals->module_enclosure ? &globals->module_enclosure->Context() : nullptr));
      admit(body.function.allocated_bytes, 1); admit(body.context.AllocatedBytes(), 1); step(body.context.Work());
      auto syntax = Take<ResolvedSyntax>(ResolveFunctionSyntax(file, header, index, body.context,
          body.function.body, body.function.values.graph, destination().first_owner, budget));
      ++next_owner;
      admit(syntax.allocated_bytes, 1); step(syntax.work);
      std::optional<ModuleAliases> aliases;
      if (globals && globals->module_enclosure) {
        const auto& enclosure = *globals->module_enclosure;
        aliases = Take<ModuleAliases>(BindModuleAliases(file, header, index, body.function.input,
            body.function.values, enclosure.Context(), enclosure.Owner(),
            {static_cast<std::uint32_t>(syntax.syntax.names.size())}, budget));
        admit(aliases->allocated_bytes, 1); step(aliases->work);
        admit(aliases->aliases.size(), sizeof(ModuleSyntax));
        syntax.syntax.modules = aliases->aliases;
        admit(syntax.syntax.modules.capacity() - aliases->aliases.size(), sizeof(ModuleSyntax));
        const auto name_count = syntax.syntax.names.size() + aliases->names.size();
        admit(name_count, sizeof(std::u16string));
        syntax.syntax.names.reserve(name_count);
        admit(syntax.syntax.names.capacity() - name_count, sizeof(std::u16string));
        for (const auto& name : aliases->names) {
          admit(name.size() + 1, sizeof(char16_t));
          syntax.syntax.names.push_back(name);
          admit(syntax.syntax.names.back().capacity() - name.size(), sizeof(char16_t));
        }
      }
      const auto constants = syntax.View();
      auto control = Take<ControlPlan>(BuildControlPlan(body.function.values.graph, budget));
      admit(control.allocated_bytes, 1); step(control.work);
      auto evaluation = Take<EvaluationPlan>(BuildEvaluationPlan(body.function.values.graph, budget, IncomingCells::kReject, {},
          aliases ? std::span<const ModuleEvaluation>(aliases->evaluations) : std::span<const ModuleEvaluation>{}));
      admit(evaluation.allocated_bytes, 1); step(evaluation.work);
      auto built = Take<ConstructedFunction>(BuildFunctionTree(body.function.values.graph, control,
          evaluation, std::move(syntax.syntax), constants, budget));
      admit(built.allocated_bytes, 1); step(built.work);
      if (aliases) step(Take<std::uint64_t>(VerifyModuleAliasTree(*aliases, evaluation, built.tree, budget)));
      if(!built.flow)throw ClassTreeFailure{Error::kCreation};
      fields = ClassStaticFields{*initializer, std::move(body.context), std::move(built.tree), body.function.input.Record(),std::move(*built.flow)};
    }
    if (file.CheckUnchanged()) return ClassTreeFailure{abc::MetadataError::kFileChanged};
    tree.initializer = std::move(fields);
    tree.allocated_bytes = initial.bytes - budget.bytes; tree.work = initial.work - budget.work;
    return tree;
  } catch (const ClassTreeFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return ClassTreeFailure{Error::kResourceLimit}; }
    catch (const std::length_error&) { return ClassTreeFailure{Error::kResourceLimit}; }
}
}
