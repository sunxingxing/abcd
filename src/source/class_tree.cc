#include "class_tree.h"
#include <bit>
#include <new>
#include <stdexcept>
#include <type_traits>
#include "runtime_name.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/global_reference.h"
namespace abcd::source {
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
std::variant<ClassTree, ClassTreeFailure> BuildClassTree(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions, const analysis::ClassMethodSites& classes,
    const FileFunction& creator, analysis::OpId operation, RouteInputIdentity identity,
    const GlobalScope* globals, analysis::SemanticBudget budget, const ClassInitializerCall* initializer) {
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
    admit(name->size() + 1, sizeof(char16_t)); step(name->size());
    std::u16string spelling(name->begin(), name->end());
    admit(spelling.capacity() - spelling.size(), sizeof(char16_t));
    const FamilyDestination destination{Strictness::kStrict, globals, FunctionFragment::kClassMember};
    auto constructor = Take<PreparedFamily>(PreparedFamily::PrepareConstructor(file, header, index, lookup,
        roles, functions, creator, operation, identity, destination, budget));
    admit(constructor.AllocatedBytes(), 1); step(constructor.Work());
    std::vector<PreparedFamily> members;
    admit(buffer.properties.size(), sizeof(PreparedFamily)); members.reserve(buffer.properties.size());
    admit(members.capacity() - buffer.properties.size(), sizeof(PreparedFamily));
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
      auto member = Take<PreparedFamily>(PreparedFamily::PrepareClass(file, header, index, lookup, roles,
          functions, classes, *occurrence, identity, destination, budget));
      admit(member.AllocatedBytes(), 1); step(member.Work());
      members.push_back(std::move(member));
    }
    std::optional<ClassStaticFields> fields;
    if (initializer) {
      if (initializer->creation != operation) return ClassTreeFailure{Error::kCreation};
      auto body = Take<ClassInitializerFunction>(ReadClassInitializerFunction(file, header, index, lookup,
          roles, functions, creator, *initializer, budget,
          globals && globals->module_enclosure ? &globals->module_enclosure->Context() : nullptr));
      admit(body.function.allocated_bytes, 1); admit(body.context.AllocatedBytes(), 1); step(body.context.Work());
      auto syntax = Take<ResolvedSyntax>(ResolveFunctionSyntax(file, header, index, body.context,
          body.function.body, body.function.values.graph, {1}, budget));
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
      fields = ClassStaticFields{*initializer, std::move(body.context), std::move(built.tree), body.function.input.Record()};
    }
    if (file.CheckUnchanged()) return ClassTreeFailure{abc::MetadataError::kFileChanged};
    return ClassTree{identity, site, std::move(spelling), std::move(constructor), std::move(members), std::move(fields),
        initial.bytes - budget.bytes, initial.work - budget.work};
  } catch (const ClassTreeFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return ClassTreeFailure{Error::kResourceLimit}; }
    catch (const std::length_error&) { return ClassTreeFailure{Error::kResourceLimit}; }
}
}
