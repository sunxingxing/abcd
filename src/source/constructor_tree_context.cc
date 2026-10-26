#include "class_tree_context.h"
#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include "function_file.h"
#include "abcd/abc/profile_capabilities.h"
namespace abcd::source {
std::variant<ClassTreeContext, ClassContextFailure>
ClassTreeContext::ResolveConstructor(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, ClassCreator creator,
    analysis::OpId operation, RouteInputIdentity identity,
    analysis::SemanticBudget budget) {
  using Error = ClassContextError;
  const auto fail = [](Error error) { return ClassContextFailure{error}; };
  try {
    const auto initial = budget;
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > budget.bytes / width) throw fail(Error::kResourceLimit);
      budget.bytes -= count * width;
    };
    const auto step = [&](std::size_t count) {
      if (count > budget.work) throw fail(Error::kWorkLimit);
      budget.work -= count;
    };
    admit(1, sizeof(ClassTreeContext));
    if (identity.profile != header.profile || identity.file_size != header.file_size ||
        identity.abc_version != header.version) return fail(Error::kProfile);
    if (file.Size() != header.file_size || file.CheckUnchanged()) return ClassContextFailure{abc::MetadataError::kFileChanged};
    if (!abc::HasPinnedRuntimeSemantics(header.profile)) return fail(Error::kProfile);
    // Caller supplies a fresh unchanged file-backed input/value pair, as for
    // family capture binding. Resolve one occurrence in O(log M + log R), never
    // decode and rescan the entire creator for every class in a large record.
    const auto& graph = creator.Graph();
    if (const auto* normal = creator.Normal();
        normal && (normal->values.identity != identity ||
                   normal->input.Method() != normal->values.method))
      return fail(Error::kSite);
    if (creator.Identity() != identity || operation.value >= graph.ops.size())
      return fail(Error::kSite);
    const auto& op = graph.ops[operation.value];
    if (op.life != analysis::ValueLife::kLive || op.origin.value >= graph.origins.size() ||
        op.use_count != 1 || op.first_use.value >= graph.uses.size()) return fail(Error::kSite);
    const auto& origin = graph.origins[op.origin.value];
    if (origin.disposition != analysis::OriginDisposition::kRetained ||
        origin.site.value >= graph.input.sites.size() ||
        graph.input.sites[origin.site.value] != operation ||
        origin.site.value >= creator.Instructions().size())
      return fail(Error::kSite);
    const auto& original = creator.Instructions()[origin.site.value];
    const auto* creation = std::get_if<analysis::ClassCreation>(&original.operation);
    const auto* working = std::get_if<analysis::ValueClassCreation>(&op.operation);
    if (!creation || !working || origin.pc != original.pc ||
        creation->constructor != working->constructor || creation->literal != working->literal ||
        creation->length != working->length || creation->cache_slot != working->cache_slot || creation->kind != working->kind)
      return fail(Error::kKind);
    const auto& use = graph.uses[op.first_use.value];
    const auto* parent = std::get_if<analysis::ValueId>(&use.operand);
    if (use.life != analysis::ValueLife::kLive || use.owner != decltype(use.owner){operation} ||
        use.role != analysis::OperandRole::kHeritage || use.purpose != analysis::ReadPurpose::kProtocol ||
        !parent || parent->value >= graph.values.size() ||
        graph.values[parent->value].life != analysis::ValueLife::kLive) return fail(Error::kKind);
    const auto domain = graph.values[parent->value].domain;
    const bool derived = domain != analysis::ValueKinds::Hole();
    // OH7 class inheritance distinguishes the absent-heritage Hole from a
    // language-valued extends operand. This resolves callable context only;
    // retaining/evaluating the actual parent and its possible throw remains
    // the enclosing class tree's obligation (including null/Proxy parents).
    if (derived && !domain.IsLanguage()) return fail(Error::kKind);
    const auto pc = original.pc;
    const auto method = creator.Method();
    step(1);
    const auto resolved = abc::ResolvePoolReference(file, header, index, method,
        creation->constructor, abc::PoolEntityKind::kMethod);
    const auto* target = std::get_if<abc::PoolReference>(&resolved);
    if (!target) return ClassContextFailure{std::get<abc::MetadataError>(resolved)};
    step(std::bit_width(index.methods.size()) + std::bit_width(index.regions.size()) + 1);
    const auto position = lookup.Find(target->offset);
    if (!position || position->value >= index.methods.size() || !index.methods[position->value].code)
      return fail(Error::kSite);
    if (roles.At(*position) != analysis::MethodRole::kOther) return fail(Error::kRole);
    const auto& constructor = index.methods[position->value];
    const auto attributes = abc::DecodeMethodAttributes(constructor.access_flags, header.profile);
    const auto* flags = std::get_if<abc::MethodAttributes>(&attributes);
    if (!flags) return ClassContextFailure{std::get<abc::MethodAttributeError>(attributes)};
    // OH7 RuntimeSetClassInheritanceRelationship (runtime_stubs-inl.h):
    // Hole sets CLASS_CONSTRUCTOR; null/non-Hole sets DERIVED_CONSTRUCTOR.
    // DERIVED is a runtime kind, not a distinct serialized method attribute.
    if (flags->sendable || (flags->kind != abc::FunctionKind::kBaseConstructor &&
        flags->kind != abc::FunctionKind::kNone)) return fail(Error::kKind);
    const auto call = abc::ReadCallType(file, header, index, constructor.annotations, {budget.bytes});
    if (const auto* error = std::get_if<abc::MetadataError>(&call)) return ClassContextFailure{*error};
    const auto frame = abc::BuildFrameLayout(constructor.code->registers, constructor.code->arguments,
        abc::DecodeCallFlags(std::get<abc::CallType>(call).value));
    if (const auto* error = std::get_if<abc::FrameError>(&frame)) return ClassContextFailure{*error};
    if (std::get<abc::FrameLayout>(frame).parameters < creation->length) return fail(Error::kSignature);
    ClassTreeContext context;
    context.identity_ = identity; context.position_ = *position;
    context.site_ =
        ClassConstructorSite{target->offset, method, pc, *creation,
                             creator.Phase() ? origin.original : std::nullopt};
    context.frame_ = std::get<abc::FrameLayout>(frame);
    context.callable_ = derived ? CallableKind::kDerivedConstructor : CallableKind::kBaseConstructor;
    context.name_ = u"constructor";
    admit(context.name_.capacity() + 1, sizeof(char16_t));
    // A stored instance initializer of THIS creation, written in the very
    // environment the constructor closes over (its depth-0 slot at entry).
    for (const auto& instance : creator.Instances()) {
      step(1);
      if (instance.creation != operation) continue;
      const auto* environments = creator.Environments();
      if (context.instance_initializer_ || instance.slot.depth.value != 0 || !environments ||
          instance.store.value >= environments->flow.before.size() ||
          !environments->flow.before[instance.store.value] || !environments->flow.before[operation.value] ||
          *environments->flow.before[instance.store.value] != *environments->flow.before[operation.value])
        return fail(Error::kSite);
      context.instance_initializer_ = instance.slot.slot;
    }
    context.work_ = initial.work - budget.work;
    if (file.CheckUnchanged()) return ClassContextFailure{abc::MetadataError::kFileChanged};
    return context;
  } catch (const ClassContextFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return fail(Error::kResourceLimit); }
    catch (const std::length_error&) { return fail(Error::kResourceLimit); }
}
}
