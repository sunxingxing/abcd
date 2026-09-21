#include "abcd/abc/method_target.h"

namespace abcd::abc {
MethodTargetResult ResolveMethodTarget(const corpus::AbcFile& file,
                                       const Header& header,
                                       const MetadataIndex& index,
                                       const MethodLookup& lookup,
                                       EntityOffset caller, PoolIndex slot) {
  const auto enclosing = lookup.Find(caller);
  if (!enclosing) return MethodTargetError::kCallerMissing;
  if (enclosing->value >= index.methods.size() ||
      index.methods[enclosing->value].offset != caller)
    return MethodTargetError::kIndexMismatch;
  const auto resolved = ResolvePoolReference(file, header, index, caller, slot,
                                             PoolEntityKind::kMethod);
  if (const auto* error = std::get_if<MetadataError>(&resolved)) return *error;
  const auto offset = std::get<PoolReference>(resolved).offset;
  const auto target = lookup.Find(offset);
  if (!target) return MethodTargetError::kTargetMissing;
  if (target->value >= index.methods.size() ||
      index.methods[target->value].offset != offset)
    return MethodTargetError::kIndexMismatch;
  const auto attributes = DecodeMethodAttributes(
      index.methods[target->value].access_flags, header.profile);
  if (const auto* error = std::get_if<MethodAttributeError>(&attributes))
    return *error;
  return MethodTarget{*target, std::get<MethodAttributes>(attributes)};
}
}  // namespace abcd::abc
