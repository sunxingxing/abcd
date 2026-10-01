#include "class_tree_context.h"
#include "creation_tree_context.h"
#include "function_file.h"
#include "element_name.h"
#include "abcd/abc/string_entity.h"
#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>
namespace abcd::source {
std::variant<ClassTreeContext, ClassContextFailure> ClassTreeContext::ResolveDefinedMember(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions, const FileFunction& creator,
    analysis::OpId definition, abc::ClassMethodKind kind, RouteInputIdentity identity,
    analysis::SemanticBudget budget) {
  using Error = ClassContextError;
  const auto initial = budget;
  const auto fail = [](Error error) { return ClassContextFailure{error}; };
  try {
    const auto step = [&](std::size_t count = 1) {
      if (count > budget.work) throw fail(Error::kWorkLimit); budget.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > budget.bytes / width) throw fail(Error::kResourceLimit); budget.bytes -= count * width;
    };
    if (kind != abc::ClassMethodKind::kGetter && kind != abc::ClassMethodKind::kSetter && kind != abc::ClassMethodKind::kOrdinary) return fail(Error::kKind);
    if (identity != creator.input.Identity()) return fail(Error::kProfile);
    const auto& graph = creator.values.graph;
    const auto& inventory = creator.values.definitions;
    if (inventory.revision != graph.revision) return fail(Error::kSite);
    const auto found = std::lower_bound(inventory.sites.begin(), inventory.sites.end(), definition,
        [&](const auto& site, auto id) { step(); return site.definition < id; });
    if (found == inventory.sites.end() || found->definition != definition) return fail(Error::kSite);
    const auto method_id = found->Method(kind);
    if (!method_id || method_id->value >= graph.ops.size() || definition.value >= graph.ops.size()) return fail(Error::kSite);
    const auto& operation = graph.ops[method_id->value];
    const auto* method = std::get_if<analysis::ValueFunctionCreation>(&operation.operation);
    if (!method || operation.origin.value >= graph.origins.size()) return fail(Error::kSite);
    const auto& origin = graph.origins[operation.origin.value];
    if (origin.site.value >= graph.input.sites.size() || graph.input.sites[origin.site.value] != *method_id ||
        origin.site.value >= creator.input.Input().instructions.size()) return fail(Error::kSite);
    const auto& original = creator.input.Input().instructions[origin.site.value];
    const auto* creation = std::get_if<analysis::FunctionCreation>(&original.operation);
    if (!creation || original.pc != origin.pc || creation->method != method->method ||
        creation->kind != method->kind || creation->home_object != method->home_object ||
        creation->length.value != method->length || creation->cache_slot != method->cache_slot) return fail(Error::kSite);
    const auto target = abc::ResolvePoolReference(file, header, index, creator.input.Method(), method->method, abc::PoolEntityKind::kMethod);
    if (const auto* error = std::get_if<abc::MetadataError>(&target)) return ClassContextFailure{*error};
    const auto occurrence = functions.FindOccurrence(std::get<abc::PoolReference>(target).offset, creator.input.Method(), origin.pc);
    if (!occurrence) return fail(Error::kSite);
    auto resolved = CreationTreeContext::Resolve(file, header, index, lookup, roles, functions,
        *occurrence, identity, budget, CreationContextMode::kMethodBody);
    if (const auto* error = std::get_if<CreationContextFailure>(&resolved)) {
      return std::visit([&](const auto& nested) -> ClassContextFailure {
        if constexpr (std::is_same_v<std::decay_t<decltype(nested)>, CreationContextError>) {
          if (nested == CreationContextError::kResourceLimit) return fail(Error::kResourceLimit);
          if (nested == CreationContextError::kWorkLimit) return fail(Error::kWorkLimit);
          return fail(Error::kSite);
        } else return ClassContextFailure{nested};
      }, error->error);
    }
    const auto& context = std::get<CreationTreeContext>(resolved);
    admit(context.AllocatedBytes(), 1); step(context.Work());
    // Native get/set syntax cannot represent an async descriptor function.
    // Ordinary definitions retain the factory's actual callable kind instead.
    if (kind != abc::ClassMethodKind::kOrdinary && context.Callable() != CallableKind::kMethod)
      return fail(Error::kKind);
    const auto length = kind == abc::ClassMethodKind::kGetter ? 0U : 1U;
    if (kind != abc::ClassMethodKind::kOrdinary && (context.Frame().parameters != length || context.Site().creation.length.value != length)) return fail(Error::kSignature);
    const auto read = [&](abc::PoolIndex key) -> std::u16string {
      const auto reference = abc::ResolvePoolReference(file, header, index, creator.input.Method(), key, abc::PoolEntityKind::kString);
      if (const auto* error = std::get_if<abc::MetadataError>(&reference)) throw ClassContextFailure{*error};
      auto decoded = abc::ReadStringEntity(file, header, std::get<abc::PoolReference>(reference).offset, {budget.bytes});
      if (const auto* error = std::get_if<abc::MetadataError>(&decoded)) throw ClassContextFailure{*error};
      auto name = std::get<std::u16string>(std::move(decoded));
      admit(name.capacity() + 1, sizeof(char16_t)); step(name.size()); return name;
    };
    if (found->prototype) {
      if (found->prototype->value >= graph.ops.size()) return fail(Error::kSite);
      const auto* prototype = std::get_if<analysis::ValueNamedRead>(&graph.ops[found->prototype->value].operation);
      if (!prototype || read(prototype->name) != u"prototype") return fail(Error::kName);
    }
    auto name = read(found->key);
    // OH7 RuntimeStOwnByName preserves the function name/kind. Accessor
    // installation instead sets the kind whose lazy NameGetter adds get/set.
    // Both native forms need the actual method name to equal the property key;
    // do not infer that equality from an element-name spelling trick.
    if (name != context.Name() || IsElementName(name) || name == u"constructor" ||
        (!found->prototype && (name == u"prototype" || name == u"name" || name == u"length"))) return fail(Error::kName);
    if (file.CheckUnchanged()) return ClassContextFailure{abc::MetadataError::kFileChanged};
    admit(1, sizeof(ClassTreeContext));
    ClassTreeContext result;
    result.identity_ = identity; result.position_ = context.Position(); result.frame_ = context.Frame();
    result.name_ = std::move(name); result.callable_ = context.Callable();
    result.site_ = ClassDefinedMember{context.Site(), definition,
        found->prototype ? abc::ClassPlacement::kPrototype : abc::ClassPlacement::kStatic, kind};
    result.work_ = initial.work - budget.work;
    return result;
  } catch (const ClassContextFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return fail(Error::kResourceLimit); }
    catch (const std::length_error&) { return fail(Error::kResourceLimit); }
}
}
