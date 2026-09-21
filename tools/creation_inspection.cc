#include "creation_inspection.h"

#include <type_traits>

namespace abcd::tools {
namespace {
const char* Category(analysis::EnvironmentFlowError) {
  return "environment_flow";
}
const char* Category(analysis::LexicalError) { return "lexical"; }
const char* Category(analysis::EnvironmentError) { return "environment"; }
const char* Category(analysis::CreationError) { return "creation"; }
const char* Category(analysis::CreationBindingError) { return "binding"; }
const char* Category(abc::MethodTargetError) { return "target"; }
const char* Category(abc::MetadataError) { return "metadata"; }
const char* Category(abc::MethodAttributeError) { return "attribute"; }
}  // namespace

CreationCounts& CreationCounts::operator+=(const CreationCounts& other) {
  methods += other.methods;
  rejected_methods += other.rejected_methods;
  sites += other.sites;
  bound += other.bound;
  failed += other.failed;
  unreachable += other.unreachable;
  local_captures += other.local_captures;
  external_captures += other.external_captures;
  sendable_captures += other.sendable_captures;
  for (const auto& [error, count] : other.obstacles) obstacles[error] += count;
  for (const auto& [opcode, count] : other.other_method_instructions)
    other_method_instructions[opcode] += count;
  return *this;
}

void InspectCreations(const corpus::AbcFile& file, const abc::Header& header,
                      const abc::MetadataIndex& index,
                      const abc::MethodLookup& lookup,
                      const abc::MethodInfo& method,
                      const abc::MethodBody& body,
                      const analysis::NormalFlow& graph,
                      CreationCounts* counts) {
  const auto analyzed = analysis::AnalyzeEnvironments(
      body, graph,
      std::uint64_t(method.code->registers) + method.code->arguments);
  if (const auto* error =
          std::get_if<analysis::EnvironmentFlowFailure>(&analyzed)) {
    ++counts->rejected_methods;
    CreationObstacle obstacle = error->kind;
    if (error->lexical_error) obstacle = *error->lexical_error;
    if (error->environment_error) obstacle = *error->environment_error;
    ++counts->obstacles[obstacle];
    return;
  }
  ++counts->methods;
  const auto& flow = std::get<analysis::EnvironmentFlow>(analyzed);
  for (std::size_t i = 0; i < body.instructions.size(); ++i) {
    const auto result = analysis::BindCreation(file, header, index, lookup,
                                               method.offset, body, flow, i);
    if (const auto* error = std::get_if<analysis::CreationError>(&result)) {
      if (*error == analysis::CreationError::kUnsupportedOpcode) {
        const auto& decoded = body.instructions[i].decoded;
        for (std::size_t operand = 0; operand < decoded.operand_count;
             ++operand)
          if (decoded.operands[operand].kind == abc::OperandKind::kMethod) {
            ++counts->other_method_instructions[decoded.opcode];
            break;
          }
        continue;
      }
    }
    ++counts->sites;
    if (const auto* error =
            std::get_if<analysis::CreationBindingError>(&result))
      if (*error == analysis::CreationBindingError::kUnreachable) {
        ++counts->unreachable;
        continue;
      }
    if (const auto* binding = std::get_if<analysis::CreationBinding>(&result)) {
      ++counts->bound;
      if (const auto* ordinary =
              std::get_if<analysis::OrdinaryCapture>(&binding->capture)) {
        if (std::holds_alternative<analysis::EnvironmentId>(
                ordinary->environment))
          ++counts->local_captures;
        else
          ++counts->external_captures;
      } else {
        ++counts->sendable_captures;
      }
    } else {
      ++counts->failed;
      std::visit(
          [&](const auto& error) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(error)>,
                                          analysis::CreationBinding>)
              ++counts->obstacles[error];
          },
          result);
    }
  }
}

nlohmann::json SerializeCounts(const CreationCounts& counts) {
  auto obstacles = nlohmann::json::array();
  for (const auto& [error, count] : counts.obstacles)
    obstacles.push_back(std::visit(
        [&](auto kind) {
          return nlohmann::json{{"category", Category(kind)},
                                {"error", static_cast<int>(kind)},
                                {"count", count}};
        },
        error));
  auto other = nlohmann::json::array();
  for (const auto& [opcode, count] : counts.other_method_instructions)
    other.push_back(
        {{"opcode", static_cast<std::uint16_t>(opcode)}, {"count", count}});
  return {{"creation_methods", counts.methods},
          {"creation_rejected_methods", counts.rejected_methods},
          {"creation_sites", counts.sites},
          {"creation_bound_sites", counts.bound},
          {"creation_failed_sites", counts.failed},
          {"creation_unreachable_sites", counts.unreachable},
          {"creation_local_captures", counts.local_captures},
          {"creation_external_captures", counts.external_captures},
          {"creation_sendable_captures", counts.sendable_captures},
          {"creation_obstacles", std::move(obstacles)},
          {"other_method_instructions", std::move(other)}};
}
}  // namespace abcd::tools
