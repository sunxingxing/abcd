#include "abcd/analysis/property_write.h"

#include "abcd/analysis/named_write.h"

namespace abcd::analysis {
std::variant<PropertyWrite, PropertyWriteError> LiftPropertyWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size) {
  const auto own = LiftOwnWrite(instruction, frame_size);
  if (const auto* write = std::get_if<OwnWrite>(&own)) {
    if (write->name_setting != PropertyNameSetting::kNone)
      return PropertyWriteError::kUnsupportedOpcode;
    return PropertyWrite{write->kind == OwnWriteKind::kStore
                             ? PropertyWriteKind::kOwnStore
                             : PropertyWriteKind::kDefineField,
                         write->name, write->object};
  }
  switch (std::get<OwnWriteError>(own)) {
    case OwnWriteError::kRegisterRange:
      return PropertyWriteError::kRegisterRange;
    case OwnWriteError::kMalformedInstruction:
      return PropertyWriteError::kMalformedInstruction;
    case OwnWriteError::kUnsupportedOpcode:
      break;
  }
  const auto named = LiftNamedWrite(instruction, frame_size);
  if (const auto* write = std::get_if<NamedWrite>(&named)) {
    if (write->lookup != PropertyLookup::kObject) return PropertyWriteError::kUnsupportedOpcode;
    return PropertyWrite{PropertyWriteKind::kAssignment, write->name,
                         write->object};
  }
  switch (std::get<NamedWriteError>(named)) {
    case NamedWriteError::kRegisterRange:
      return PropertyWriteError::kRegisterRange;
    case NamedWriteError::kMalformedInstruction:
      return PropertyWriteError::kMalformedInstruction;
    case NamedWriteError::kUnsupportedOpcode:
      return PropertyWriteError::kUnsupportedOpcode;
  }
  std::terminate();
}
}  // namespace abcd::analysis
