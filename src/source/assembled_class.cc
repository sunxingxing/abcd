#include "class_assembly.h"

#include <algorithm>
#include <new>

#include "class_emission.h"
#include "abcd/source/static_fields.h"

namespace abcd::source {
CreatedClassResult WriteAssembledClass(const ClassExpression& expression,
    const MethodConstants& constants, std::optional<ClassParent> parent,
    std::ostream& output, LocalLexicalBudget budget, CreationExpansion* expansion) {
  try {
    if (!constants.leaf_methods) return CreatedClassError::kMembers;
    const auto read_key = [&](abc::PoolIndex key) {
      const auto pool = TakeClassValue<abc::PoolReference>(abc::ResolvePoolReference(
          constants.file, constants.header, constants.index, constants.method,
          key, abc::PoolEntityKind::kString));
      auto strings = constants.string_budget;
      strings.bytes = std::min(strings.bytes, budget.bytes);
      auto text = TakeClassValue<std::u16string>(abc::ReadStringEntity(
          constants.file, constants.header, pool.offset, strings));
      if (text.capacity() > budget.bytes / sizeof(char16_t))
        throw ClassFailure{CreatedClassError::kResourceLimit};
      return text;
    };
    // This is a literal property-key boundary, not string-based opcode or IR
    // dispatch. Only the immutable own prototype property grants class identity.
    if (expression.prototype_key && read_key(*expression.prototype_key) != u"prototype")
      return CreatedClassError::kAccessorTarget;
    std::size_t accessors_count = 0;
    for (const auto& member : expression.assembled_members)
      if (member.kind != abc::ClassMethodKind::kOrdinary) ++accessors_count;
    const auto attached_count = expression.assembled_members.size() - accessors_count;
    AdmitClassBytes(accessors_count, sizeof(ClassAccessor), budget.bytes);
    AdmitClassBytes(attached_count, sizeof(ClassAttachedMethod), budget.bytes);
    std::vector<ClassAccessor> accessors;
    std::vector<ClassAttachedMethod> attached;
    accessors.reserve(accessors_count);
    attached.reserve(attached_count);
    AdmitClassBytes(accessors.capacity() - accessors_count, sizeof(ClassAccessor), budget.bytes);
    AdmitClassBytes(attached.capacity() - attached_count, sizeof(ClassAttachedMethod), budget.bytes);
    for (const auto& member : expression.assembled_members) {
      auto key = read_key(member.key);
      AdmitClassBytes(key.capacity(), sizeof(char16_t), budget.bytes);
      const auto target = TakeClassValue<abc::MethodTarget>(abc::ResolveMethodTarget(
          constants.file, constants.header, constants.index, *constants.leaf_methods,
          constants.method, member.creation.method));
      switch (member.kind) {
        case abc::ClassMethodKind::kOrdinary:
          attached.push_back({std::move(key), target.position, member.placement,
                              member.creation.length.value});
          break;
        case abc::ClassMethodKind::kGetter:
        case abc::ClassMethodKind::kSetter:
          accessors.push_back({std::move(key), target.position, member.placement,
                               member.kind, member.creation.length.value});
          break;
        case abc::ClassMethodKind::kGenerator:
        case abc::ClassMethodKind::kAsyncGenerator:
          return CreatedClassError::kMembers;
      }
    }
    std::optional<StaticFields> fields;
    if (expression.initializer) {
      const auto caller = constants.leaf_methods->Find(constants.method);
      const auto target = TakeClassValue<abc::MethodTarget>(abc::ResolveMethodTarget(
          constants.file, constants.header, constants.index, *constants.leaf_methods,
          constants.method, expression.initializer->method));
      if (!caller || target.attributes.sendable ||
          target.attributes.kind != abc::FunctionKind::kNone ||
          constants.index.methods[target.position.value].owner_offset !=
              constants.index.methods[caller->value].owner_offset)
        return CreatedClassError::kMembers;
      auto recovered = RecoverStaticFields(constants.file, constants.header,
          constants.index, constants.index.methods[target.position.value],
          budget.bytes, constants.string_budget, constants.module);
      if (const auto* failure = std::get_if<StaticFieldFailure>(&recovered)) {
        switch (failure->kind) {
          case StaticFieldError::kResourceLimit: return CreatedClassError::kResourceLimit;
          case StaticFieldError::kRead: return abc::MetadataError::kRead;
          case StaticFieldError::kFileChanged: return abc::MetadataError::kFileChanged;
          default: return CreatedClassError::kStaticFieldValue;
        }
      }
      fields.emplace(std::get<StaticFields>(std::move(recovered)));
      AdmitClassBytes(fields->AllocatedBytes(), 1, budget.bytes);
    }
    return WriteCreatedClass(constants.file, constants.header, constants.index,
        *constants.leaf_methods, constants.method, expression.creation, expression.origin,
        {expression.origin, expression.heritage}, output, budget,
        constants.string_budget, constants.roles, constants.module,
        constants.global_scope ? &*constants.global_scope : nullptr,
        fields ? &*fields : nullptr, accessors, parent, attached, expansion);
  } catch (const ClassFailure& failure) {
    return failure.result;
  } catch (const std::bad_alloc&) {
    return CreatedClassError::kResourceLimit;
  }
}
}  // namespace abcd::source
