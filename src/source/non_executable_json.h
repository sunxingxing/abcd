#ifndef ABCD_SOURCE_NON_EXECUTABLE_JSON_H_
#define ABCD_SOURCE_NON_EXECUTABLE_JSON_H_
#include "abcd/analysis/non_executable.h"
#include <nlohmann/json.hpp>
namespace abcd::source {
inline nlohmann::json NonExecutableJson(const analysis::NonExecutableEvidence& e) {
  return {{"opcode", static_cast<unsigned>(e.opcode)}, {"evidence", static_cast<unsigned>(e.evidence)}};
}
inline std::optional<analysis::NonExecutableEvidence> ParseNonExecutable(const nlohmann::json& j) {
  if (!j.is_object() || !j.contains("opcode") || !j.contains("evidence") ||
      !j["opcode"].is_number_unsigned() || !j["evidence"].is_number_unsigned()) return {};
  const auto opcode = j["opcode"].get<std::uint64_t>();
  if (opcode > UINT16_MAX) return {};
  const auto e = analysis::NonExecutable(static_cast<abc::Opcode>(opcode));
  if (!e || j["evidence"].get<std::uint64_t>() != static_cast<unsigned>(e->evidence)) return {};
  return e;
}
}
#endif
