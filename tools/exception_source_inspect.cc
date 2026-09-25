#include "exception_source_inspect.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "abcd/analysis/exception_calls.h"

void InspectExceptionSource(const abcd::source::ExceptionAstInput& input,
    abcd::analysis::SemanticRegionTree regions,
    const abcd::source::MethodConstants& constants, std::uint64_t frame_size,
    std::size_t bytes, std::ostream& output,
    abcd::analysis::CapturedValues captured_values) {
  using namespace abcd;
  const auto emit_failure = [&](const source::SourceFailure& failure) {
    output << nlohmann::json{{"exception_source_error", static_cast<unsigned>(failure.kind)},
                             {"pc", failure.pc.value}} << '\n';
  };
  // Expose route candidates independently of input-value admission; they are
  // not AST/source success. Release this inspection plan before AST building.
  if (regions.allocated_bytes <= bytes) {
    const auto indexed = analysis::InstructionSsa::Build(input.flow, input.ssa,
        bytes - regions.allocated_bytes);
    const auto* ssa = std::get_if<analysis::InstructionSsa>(&indexed);
    const auto routed = ssa ? analysis::ProveExceptionCalls(input.flow, *ssa,
        {bytes - regions.allocated_bytes - ssa->AllocatedBytes(), analysis::kDefaultSemanticWork})
        : std::variant<analysis::ExceptionCalls, analysis::ExceptionCallError>{
            std::get<analysis::InstructionSsaError>(indexed) == analysis::InstructionSsaError::kResourceLimit
                ? analysis::ExceptionCallError::kResourceLimit : analysis::ExceptionCallError::kInvalidFlow};
    if (const auto* calls = std::get_if<analysis::ExceptionCalls>(&routed)) {
      for (std::size_t i = 0; i < calls->routes.size(); ++i)
        if (calls->routes[i])
          output << nlohmann::json{{"receiver_route_call", i},
              {"pc", input.flow.instructions[i].pc.value},
              {"property_pc", input.flow.instructions[calls->routes[i]->property.value].pc.value},
              {"preserves_argument_order", calls->routes[i]->evaluation ==
                  analysis::ReceiverEvaluation::kPreserveOrder},
              {"requires_forward_region", calls->routes[i]->region ==
                  analysis::ReceiverRegion::kForward}} << '\n';
    } else {
      output << nlohmann::json{{"receiver_route_error", static_cast<unsigned>(
          std::get<analysis::ExceptionCallError>(routed))}} << '\n';
    }
  }
  const auto lowered = source::BuildExceptionSourceAst(input, std::move(regions),
      {bytes, analysis::kDefaultSemanticWork});
  const auto* ast = std::get_if<source::SourceAst>(&lowered);
  output << nlohmann::json{{"exception_ast_proven", ast != nullptr}} << '\n';
  if (!ast) {
    emit_failure(std::get<source::SourceFailure>(lowered));
    return;
  }
  try {
    if (ast->allocated_bytes > bytes) throw source::SourceFailure{source::SourceError::kResourceLimit, {}};
    bytes -= ast->allocated_bytes;
    // Fixed decimal register-name storage avoids per-register allocations.
    constexpr std::string_view kPrefix = "$r";
    constexpr auto kDigits = std::numeric_limits<std::uint32_t>::digits10 + 1;
    using RegisterName = std::array<char, kPrefix.size() + kDigits>;
    constexpr auto kNameBytes = sizeof(RegisterName) + sizeof(std::string_view);
    if (frame_size > UINT32_MAX || frame_size > bytes / kNameBytes)
      throw source::SourceFailure{source::SourceError::kResourceLimit, {}};
    std::vector<RegisterName> storage(static_cast<std::size_t>(frame_size));
    std::vector<std::string_view> names;
    if (storage.capacity() > bytes / sizeof(RegisterName))
      throw source::SourceFailure{source::SourceError::kResourceLimit, {}};
    bytes -= storage.capacity() * sizeof(RegisterName);
    names.reserve(storage.size());
    if (names.capacity() > bytes / sizeof(std::string_view))
      throw source::SourceFailure{source::SourceError::kResourceLimit, {}};
    bytes -= names.capacity() * sizeof(std::string_view);
    for (std::size_t i = 0; i < storage.size(); ++i) {
      auto& name = storage[i];
      std::copy(kPrefix.begin(), kPrefix.end(), name.begin());
      const auto converted = std::to_chars(name.data() + kPrefix.size(), name.data() + name.size(), i);
      if (converted.ec != std::errc{}) throw source::SourceFailure{source::SourceError::kOutput, {}};
      names.emplace_back(name.data(), converted.ptr - name.data());
    }
    // JSON can escape one byte to six; reserve serialization headroom rather
    // than allowing a successful body to exhaust inspection's byte allowance.
    constexpr std::size_t kSerializationFactor = 16;
    auto bounded_constants = constants;
    // This detached inspector owns only the '$' private names above. Match
    // the ordinary survey's conditional global scope, never a record scope:
    // embedding must repeat collision checks against its actual bindings.
    if (!bounded_constants.global_scope) bounded_constants.global_scope = source::GlobalScope{};
    bounded_constants.string_budget.bytes = std::min(
        constants.string_budget.bytes, bytes / kSerializationFactor);
    const auto printed = source::PrintSourceTemplate(*ast, input.blocks,
        {"$acc", names}, bounded_constants, bytes / kSerializationFactor,
        captured_values == analysis::CapturedValues::kLanguage ? &input.environments : nullptr);
    const auto* source = std::get_if<source::PrintedTemplate>(&printed);
    if (!source) {
      emit_failure(std::get<source::SourceFailure>(printed));
      return;
    }
    output << nlohmann::json{{"exception_body_printed", source->captures.empty()},
                             {"source_contract", source::SourceContractName(source->contract)},
                             {"host_realm_binding", source->host_realm
                                 ? nlohmann::json(source->host_realm->binding) : nlohmann::json(nullptr)},
                             {"exception_template_printed", true},
                             {"requires_unshadowed_globals", true},
                             {"requires_module_bindings", constants.module != nullptr},
                             {"requires_parent_capture_proof", !source->captures.empty()},
                             {"executable", false}, {"body", source->text},
                             {"capture_count", source->captures.size()},
                             {"position_count", source->spans.size()}} << '\n';
    for (const auto& span : source->spans)
      output << nlohmann::json{{"pc", span.pc.value},
          {"origin_kind", source::SpanOriginName(span.origin)},
          {"begin", {span.begin.byte, span.begin.line, span.begin.column, span.begin.captures}},
          {"end", {span.end.byte, span.end.line, span.end.column, span.end.captures}}} << '\n';
    for (const auto& capture : source->captures)
      output << nlohmann::json{{"capture_depth", capture.cell.depth.value},
          {"required_domain", static_cast<unsigned>(capture.requirement)},
          {"capture_slot", capture.cell.slot.value},
          {"position", {capture.position.byte, capture.position.line,
                        capture.position.column, capture.position.captures}}} << '\n';
  } catch (const source::SourceFailure& failure) { emit_failure(failure);
  } catch (const std::bad_alloc&) { emit_failure({source::SourceError::kResourceLimit, {}});
  } catch (const std::length_error&) { emit_failure({source::SourceError::kResourceLimit, {}}); }
}
