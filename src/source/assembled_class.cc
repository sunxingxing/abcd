#include "class_assembly.h"

#include <algorithm>
#include <new>

#include "class_emission.h"

namespace abcd::source {
CreatedClassResult WriteAssembledClass(const ClassExpression& expression,
    const MethodConstants& constants, BoundClassParent parent,
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
    return WriteCreatedClass(constants.file, constants.header, constants.index,
        *constants.leaf_methods, constants.method, expression.creation, expression.origin,
        {expression.origin, analysis::HeritageKind::kUnknown}, output, budget,
        constants.string_budget, constants.roles, constants.module,
        constants.global_scope ? &*constants.global_scope : nullptr,
        nullptr, accessors, parent, attached, expansion);
  } catch (const ClassFailure& failure) {
    return failure.result;
  } catch (const std::bad_alloc&) {
    return CreatedClassError::kResourceLimit;
  }
}
}  // namespace abcd::source
