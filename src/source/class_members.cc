#include <algorithm>
#include <limits>
#include <utility>

#include "class_emission.h"
#include "runtime_name.h"

namespace abcd::source {
ClassMemberTarget ResolveClassMember(const abc::Header& header,
                                const abc::MetadataIndex& index,
                                const abc::MethodLookup& lookup,
                                const abc::ClassProperty& property,
                                const analysis::MethodRoleIndex* roles) {
  const auto* member = std::get_if<abc::ClassMethod>(&property.value);
  if (!member || member->kind != abc::ClassMethodKind::kOrdinary ||
      property.key.tag != abc::LiteralTag::kString)
    throw ClassFailure{CreatedClassError::kMembers};
  const auto position = lookup.Find(member->target);
  if (!position) throw ClassFailure{abc::MethodTargetError::kTargetMissing};
  if (position->value >= index.methods.size() ||
      index.methods[position->value].offset != member->target)
    throw ClassFailure{abc::MethodTargetError::kIndexMismatch};
  if (!roles || roles->At(*position) != analysis::MethodRole::kOrdinary)
    throw ClassFailure{CreatedClassError::kMembers};
  const auto& method = index.methods[position->value];
  const auto attributes = TakeClassValue<abc::MethodAttributes>(
      abc::DecodeMethodAttributes(method.access_flags, header.profile));
  if (attributes.sendable ||
      (attributes.kind != abc::FunctionKind::kNone &&
       attributes.kind != abc::FunctionKind::kAsync))
    throw ClassFailure{CreatedClassError::kMembers};
  // Both admitted dynamic containers execute the pinned v7 async enter /
  // resolve / reject handlers; body-level normalization still proves the
  // exact no-await promise/exception template before source publication.
  if (attributes.kind == abc::FunctionKind::kAsync &&
      header.profile != abc::ProfileId::kDynamic1206 &&
      header.profile != abc::ProfileId::kDynamic1301)
    throw ClassFailure{CreatedClassError::kMembers};
  const auto name = RuntimeName(method);
  if (!name) throw ClassFailure{CreatedClassError::kName};
  return {*position, attributes, *name};
}
std::u16string ReadClassKey(const corpus::AbcFile& file,
                            const abc::Header& header,
                            const abc::ClassProperty& property,
                            abc::StringBudget strings) {
  const auto* offset = std::get_if<abc::EntityOffset>(&property.key.value);
  if (property.key.tag != abc::LiteralTag::kString || !offset)
    throw ClassFailure{CreatedClassError::kMembers};
  return TakeClassValue<std::u16string>(
      abc::ReadStringEntity(file, header, *offset, strings));
}
namespace {
bool IsElement(std::string_view name) {
  constexpr auto kDigits = std::numeric_limits<std::uint32_t>::digits10 + 1;
  constexpr std::uint64_t kMaxIndex = UINT32_MAX - 1, kRadix = 10;
  if (name.empty() || name.size() > kDigits ||
      (name.size() > 1 && name.front() == '0'))
    return false;
  std::uint64_t value = 0;
  for (const auto ch : name) {
    if (ch < '0' || ch > '9') return false;
    value = value * kRadix + (ch - '0');
  }
  return value <= kMaxIndex;
}
bool SafeMethodKey(std::string_view name, abc::ClassPlacement placement) {
  return !IsElement(name) &&
         (placement == abc::ClassPlacement::kPrototype
              ? name != "constructor"
              : name != "name" && name != "length" &&
                    name != "prototype");
}
}  // namespace
void CheckClassMembers(const corpus::AbcFile& file, const abc::Header& header,
                       const abc::MetadataIndex& index,
                       const abc::MethodLookup& lookup,
                       const abc::ClassBuffer& members,
                       const analysis::MethodRoleIndex* roles,
                       std::size_t bytes, abc::StringBudget strings) {
  using Key = std::pair<abc::ClassPlacement, std::string_view>;
  AdmitClassBytes(members.properties.size(), sizeof(Key), bytes);
  std::vector<Key> keys(members.properties.size());
  AdmitClassBytes(keys.capacity() - keys.size(), sizeof(Key), bytes);
  for (std::size_t i = 0; i < members.properties.size(); ++i) {
    const auto& property = members.properties[i];
    const auto target =
        ResolveClassMember(header, index, lookup, property, roles);
    const auto key = ReadClassKey(file, header, property, strings);
    if (key.size() != target.name.size() ||
        !std::equal(key.begin(), key.end(), target.name.begin()))
      throw ClassFailure{CreatedClassError::kName};
    // v7.0 has distinct reserved-property/element layout paths. Until their
    // descriptor semantics are proven, do not treat them as ordinary methods.
    if (!SafeMethodKey(target.name, property.placement))
      throw ClassFailure{CreatedClassError::kMembers};
    keys[i] = {property.placement, target.name};
  }
  // Views point into immutable metadata, not transient UTF-16 keys. Sort a
  // separate index so duplicate detection never changes definition order.
  std::sort(keys.begin(), keys.end());
  if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
    throw ClassFailure{CreatedClassError::kMembers};
}
void CheckAttachedMethods(const abc::Header& header,
                          const abc::MetadataIndex& index,
                          const abc::MethodLookup& lookup,
                          const abc::ClassBuffer& members,
                          std::span<const ClassAccessor> accessors,
                          std::span<const ClassAttachedMethod> attached,
                          const analysis::MethodRoleIndex* roles,
                          abc::EntityOffset owner, std::size_t& bytes) {
  if (attached.empty()) return;
  struct Key {
    abc::ClassPlacement placement;
    std::string_view name;
    bool attached;
  };
  if (members.properties.size() > SIZE_MAX - accessors.size() ||
      members.properties.size() + accessors.size() >
          SIZE_MAX - attached.size())
    throw ClassFailure{CreatedClassError::kResourceLimit};
  const auto count = members.properties.size() + accessors.size() +
                     attached.size();
  AdmitClassBytes(count, sizeof(Key), bytes);
  std::vector<Key> keys;
  keys.reserve(count);
  AdmitClassBytes(keys.capacity() - count, sizeof(Key), bytes);
  for (const auto& property : members.properties) {
    const auto target = ResolveClassMember(
        header, index, lookup, property, roles);
    keys.push_back({property.placement, target.name, false});
  }
  for (const auto& accessor : accessors) {
    const auto name = RuntimeName(index.methods[accessor.method.value]);
    if (!name) throw ClassFailure{CreatedClassError::kAccessorTarget};
    keys.push_back({accessor.placement, *name, false});
  }
  for (const auto& method : attached) {
    if (!roles || method.method.value >= index.methods.size() ||
        roles->At(method.method) != analysis::MethodRole::kUnseen)
      throw ClassFailure{CreatedClassError::kMembers};
    const auto& target = index.methods[method.method.value];
    if (target.owner_offset != owner || !target.code)
      throw ClassFailure{CreatedClassError::kMembers};
    const auto attributes = TakeClassValue<abc::MethodAttributes>(
        abc::DecodeMethodAttributes(target.access_flags, header.profile));
    if (attributes.sendable ||
        (attributes.kind != abc::FunctionKind::kNone &&
         attributes.kind != abc::FunctionKind::kAsync) ||
        (attributes.kind == abc::FunctionKind::kAsync &&
         header.profile == abc::ProfileId::kDynamic1202))
      throw ClassFailure{CreatedClassError::kMembers};
    const auto name = RuntimeName(target);
    if (!name || !SafeMethodKey(*name, method.placement) ||
        method.key.size() != name->size() ||
        !std::equal(method.key.begin(), method.key.end(), name->begin()))
      throw ClassFailure{CreatedClassError::kName};
    keys.push_back({method.placement, *name, true});
  }
  std::sort(keys.begin(), keys.end(), [](const Key& left, const Key& right) {
    return std::pair{left.placement, left.name} <
           std::pair{right.placement, right.name};
  });
  for (std::size_t i = 1; i < keys.size(); ++i)
    if (keys[i].placement == keys[i - 1].placement &&
        keys[i].name == keys[i - 1].name &&
        (keys[i].attached || keys[i - 1].attached))
      throw ClassFailure{CreatedClassError::kMembers};
}
}  // namespace abcd::source
