#ifndef ABCD_SOURCE_GUARDED_CALL_JSON_H_
#define ABCD_SOURCE_GUARDED_CALL_JSON_H_
#include "abcd/source/call_reference.h"
#include <nlohmann/json.hpp>
namespace abcd::source {
inline nlohmann::json GuardedCallJson(const GuardedCallFailure& value) {
  nlohmann::json result{{"kind", static_cast<unsigned>(value.kind)}, {"call", value.call.value},
      {"read", value.read.value}, {"guard", value.guard.value}, {"use", value.use.value}};
  if (value.read_pc) result["read_pc"] = value.read_pc->value;
  if (value.read_working_pc) result["read_working_pc"] = value.read_working_pc->value;
  if (value.guard_pc) result["guard_pc"] = value.guard_pc->value;
  if (value.guard_working_pc) result["guard_working_pc"] = value.guard_working_pc->value;
  return result;
}
inline std::optional<GuardedCallFailure> ParseGuardedCall(const nlohmann::json& object) {
  if (!object.is_object()) return {};
  const auto number = [&](const char* key) -> std::optional<std::uint32_t> {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) return {};
    const auto value = it->get<std::uint64_t>();
    if (value > UINT32_MAX) return {};
    return static_cast<std::uint32_t>(value);
  };
  const auto kind = number("kind"), call = number("call"), read = number("read"), guard = number("guard"), use = number("use");
  if (!kind || *kind > static_cast<unsigned>(GuardedCallError::kCorrespondence) || !call || !read || !guard || !use) return {};
  GuardedCallFailure result{static_cast<GuardedCallError>(*kind), {*call}, {*read}, {*guard}, {*use}};
  const auto pc = [&](const char* key, std::optional<abc::CodeOffset>& out) {
    if (!object.contains(key)) return true;
    const auto value = number(key); if (!value) return false;
    out = abc::CodeOffset{*value}; return true;
  };
  if (!pc("read_pc", result.read_pc) || !pc("read_working_pc", result.read_working_pc) ||
      !pc("guard_pc", result.guard_pc) || !pc("guard_working_pc", result.guard_working_pc)) return {};
  return result;
}
}  // namespace abcd::source
#endif
