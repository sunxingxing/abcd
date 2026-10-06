#include "parameter_origins.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
ParameterOffsetResult MapParameterDebugNames(
    const abc::FrameLayout& frame, const abc::MethodBody& body,
    const abc::DebugScopes& scopes, abc::ProfileId profile, std::size_t bytes) {
  using Error = ParameterNameError;
  try {
    if (body.instructions.empty() || frame.parameters == 0) return Error::kUnproven;
    const auto admit = [&](std::size_t n, std::size_t width) {
      if (n > bytes / width) throw Error::kResourceLimit;
      bytes -= n * width;
    };
    const auto reserve = [&]<class T>(std::vector<T>& values, std::size_t n) {
      admit(n, sizeof(T)); values.reserve(n); admit(values.capacity() - n, sizeof(T));
    };
    std::vector<ParameterObservation> observations;
    std::vector<std::size_t> locals;
    reserve(observations, scopes.locals.size()); reserve(locals, scopes.locals.size());
    for (std::size_t i = 0; i < scopes.locals.size(); ++i) {
      const auto& local = scopes.locals[i];
      if (local.reg < 0 || static_cast<std::uint32_t>(local.reg) >= frame.registers ||
          (local.end && *local.end <= local.start)) continue;
      const auto site = std::lower_bound(body.instructions.begin(), body.instructions.end(), local.start,
          [](const abc::BodyInstruction& instruction, std::uint32_t pc) { return instruction.offset.value < pc; });
      if (site == body.instructions.end() || site->offset.value != local.start) continue;
      observations.push_back({analysis::InstructionId{static_cast<std::uint32_t>(site - body.instructions.begin())},
          abc::RegisterId{static_cast<std::uint32_t>(local.reg)}});
      locals.push_back(i);
    }
    if (observations.empty()) return Error::kUnproven;
    const auto observed = ObserveParameters(frame, body, profile, observations, bytes);
    if (const auto* error = std::get_if<Error>(&observed)) return *error;
    const auto& origins = std::get<ObservedParameters>(observed);
    admit(origins.capacity(), sizeof(std::optional<std::uint32_t>));
    std::vector<std::optional<abc::EntityOffset>> names;
    reserve(names, frame.parameters); names.resize(frame.parameters);
    for (std::size_t i = 0; i < origins.size(); ++i) {
      if (!origins[i]) continue;
      auto& name = names[*origins[i]];
      const auto candidate = scopes.locals[locals[i]].name;
      // Repeated ranges can describe the same name. Conflicting names have no
      // unique formal evidence; the final identifier/collision checks remain.
      if (name && *name != candidate) return Error::kUnproven;
      name = candidate;
    }
    if (std::any_of(names.begin(), names.end(), [](const auto& name) { return !name; }))
      return Error::kUnproven;
    ParameterOffsets result;
    reserve(result, names.size());
    for (const auto& name : names) result.push_back(*name);
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
