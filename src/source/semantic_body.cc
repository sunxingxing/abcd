#include "abcd/source/semantic_body.h"

#include <type_traits>

namespace abcd::source {
namespace {
template <analysis::CapturedValues Captures>
auto BuildBody(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes)
    -> std::conditional_t<Captures == analysis::CapturedValues::kReject,
                           SemanticBodyResult, SemanticTemplateResult> {
  if (names.registers.size() != frame.registers)
    return SourceFailure{SourceError::kInvalidEffect, {}};
  auto lifted = analysis::BuildSemanticMethod(
      body, constants.header.profile, frame.registers, budget);
  const auto* method = std::get_if<analysis::SemanticMethod>(&lifted);
  if (!method) return std::get<analysis::SemanticFailure>(lifted);
  auto verified = analysis::VerifySemanticSource(
      body, *method, frame.registers, frame.locals, budget, Captures);
  const auto* proof = std::get_if<analysis::SemanticProof>(&verified);
  if (!proof) return std::get<analysis::SemanticProofFailure>(verified);
  if (proof->allocated_bytes > budget.bytes)
    return SourceFailure{SourceError::kResourceLimit, {}};
  const auto remaining = budget.bytes - proof->allocated_bytes;
  auto recovered = analysis::BuildSemanticRegions(*method, remaining);
  auto* regions = std::get_if<analysis::SemanticRegionTree>(&recovered);
  if (!regions) return std::get<analysis::RegionFailure>(recovered);
  auto lowered = BuildSourceAst(*method, *proof, std::move(*regions), remaining);
  const auto* ast = std::get_if<SourceAst>(&lowered);
  if (!ast) return std::get<SourceFailure>(lowered);
  if constexpr (Captures == analysis::CapturedValues::kReject) {
    auto printed = PrintSourceBody(*ast, *method, names, constants, output_bytes);
    if (auto* source = std::get_if<PrintedSource>(&printed)) return std::move(*source);
    return std::get<SourceFailure>(printed);
  } else {
    auto printed = PrintSourceTemplate(*ast, *method, names, constants, output_bytes);
    if (auto* source = std::get_if<PrintedTemplate>(&printed)) return std::move(*source);
    return std::get<SourceFailure>(printed);
  }
}
}  // namespace
SemanticBodyResult BuildSemanticBody(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes) {
  return BuildBody<analysis::CapturedValues::kReject>(
      body, frame, names, constants, budget, output_bytes);
}
SemanticTemplateResult BuildSemanticTemplate(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes) {
  return BuildBody<analysis::CapturedValues::kLanguage>(
      body, frame, names, constants, budget, output_bytes);
}
}  // namespace abcd::source
