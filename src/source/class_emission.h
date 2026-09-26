#ifndef ABCD_SOURCE_CLASS_EMISSION_H_
#define ABCD_SOURCE_CLASS_EMISSION_H_
#include <type_traits>

#include "abcd/source/created_class.h"

namespace abcd::source {
struct ClassFailure {
  CreatedClassResult result;
};
template <typename T, typename... V>
T TakeClassValue(std::variant<V...> value) {
  if (!std::holds_alternative<T>(value))
    std::visit(
        [](const auto& item) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(item)>, T>)
            throw ClassFailure{item};
        },
        value);
  return std::get<T>(std::move(value));
}
inline void AdmitClassBytes(std::size_t count, std::size_t width,
                            std::size_t& bytes) {
  if (count > bytes / width)
    throw ClassFailure{CreatedClassError::kResourceLimit};
  bytes -= count * width;
}
struct ClassMemberTarget {
  abc::MethodPosition position;
  abc::MethodAttributes attributes;
  std::string_view name;
};
// Same immutable metadata/role context as WriteCreatedClass. These helpers
// throw only typed ClassFailure to the owning class emission boundary.
ClassMemberTarget ResolveClassMember(const abc::Header& header,
                                const abc::MetadataIndex& index,
                                const abc::MethodLookup& lookup,
                                const abc::ClassProperty& property,
                                const analysis::MethodRoleIndex* roles);
std::u16string ReadClassKey(const corpus::AbcFile& file,
                            const abc::Header& header,
                            const abc::ClassProperty& property,
                            abc::StringBudget strings);
void CheckClassMembers(const corpus::AbcFile& file, const abc::Header& header,
                       const abc::MetadataIndex& index,
                       const abc::MethodLookup& lookup,
                       const abc::ClassBuffer& members,
                       const analysis::MethodRoleIndex* roles,
                       std::size_t bytes, abc::StringBudget strings);
void CheckAttachedMethods(const abc::Header& header,
                          const abc::MetadataIndex& index,
                          const abc::MethodLookup& lookup,
                          const abc::ClassBuffer& members,
                          std::span<const ClassAccessor> accessors,
                          std::span<const ClassAttachedMethod> attached,
                          const analysis::MethodRoleIndex* roles,
                          abc::EntityOffset owner, std::size_t& bytes);
}  // namespace abcd::source
#endif
