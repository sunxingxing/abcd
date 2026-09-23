#include <algorithm>
#include <limits>
#include <utility>

#include "class_emission.h"
#include "runtime_name.h"

namespace abcd::source {
MemberTarget ResolveClassMember(const abc::Header& header,
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
  if (attributes.kind == abc::FunctionKind::kAsync &&
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
    if (IsElement(target.name) ||
        (property.placement == abc::ClassPlacement::kPrototype
             ? target.name == "constructor"
             : target.name == "name" || target.name == "length" ||
                   target.name == "prototype"))
      throw ClassFailure{CreatedClassError::kMembers};
    keys[i] = {property.placement, target.name};
  }
  // Views point into immutable metadata, not transient UTF-16 keys. Sort a
  // separate index so duplicate detection never changes definition order.
  std::sort(keys.begin(), keys.end());
  if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
    throw ClassFailure{CreatedClassError::kMembers};
}
}  // namespace abcd::source
