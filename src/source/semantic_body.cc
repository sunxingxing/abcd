#include "abcd/source/semantic_body.h"

#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/source/named_scope.h"
#include "module_read_snapshot.h"
#include "exception_body.h"
#include "abcd/source/capture_preservation.h"
#include "defined_captures.h"

namespace abcd::source {
namespace {
template <analysis::CapturedValues Captures>
auto BuildBody(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes)
    -> std::conditional_t<Captures == analysis::CapturedValues::kReject,
                           SemanticBodyResult, SemanticTemplateResult> {
  try {
  if (names.registers.size() != frame.registers)
    return SourceFailure{SourceError::kInvalidEffect, {}};
  std::optional<analysis::RangeDerivedConstructor> derived;
  if (names.range_derived_constructor) {
    const auto preserved = ProveCapturePreservation(
        body, constants, constants.leaf_methods, budget);
    const auto* sites = std::get_if<CapturePreservations>(&preserved);
    if (!sites) return std::get<SourceFailure>(preserved);
    if (sites->AllocatedBytes() > budget.bytes || sites->work_units > budget.work)
      return SourceFailure{SourceError::kResourceLimit, {}};
    budget.work -= sites->work_units;
    const auto checked = analysis::ProveRangeDerivedConstructor(
        body, frame, {budget.bytes - sites->AllocatedBytes(), budget.work},
        constants.header.profile, sites->sites);
    if (const auto* failure =
            std::get_if<analysis::DerivedConstructorError>(&checked))
      return SourceFailure{
          *failure == analysis::DerivedConstructorError::kResourceLimit
              ? SourceError::kResourceLimit : SourceError::kSuperInitialization,
          {}};
    derived = std::get<analysis::RangeDerivedConstructor>(checked);
  }
  auto lifted = analysis::BuildSemanticMethod(
      body, constants.header.profile, frame.registers, budget, constants.awaits);
  auto* method = std::get_if<analysis::SemanticMethod>(&lifted);
  if (!method) return std::get<analysis::SemanticFailure>(lifted);
  if (!constants.source_pcs.empty()) {
    if (constants.source_pcs.size() != method->instructions.size())
      return SourceFailure{SourceError::kInvalidEffect, {}};
    for (std::size_t i = 0; i < method->instructions.size(); ++i)
      method->instructions[i].origin = constants.source_pcs[i];
  }
  if (method->allocated_bytes > budget.bytes)
    return SourceFailure{SourceError::kResourceLimit, {}};
  const auto snapshot = SnapshotModuleReads(method->instructions, constants,
      {budget.bytes - method->allocated_bytes, budget.work});
  const auto* initialized = std::get_if<ModuleReadSnapshot>(&snapshot);
  if (!initialized) return std::get<SourceFailure>(snapshot);
  budget.bytes -= initialized->AllocatedBytes();
  budget.work -= initialized->work_units;
  auto verified = analysis::VerifySemanticSource(
      body, *method, frame.registers, frame.locals, budget, Captures,
      initialized->loads, analysis::ClosureWrites::kLanguage);
  auto* proof = std::get_if<analysis::SemanticProof>(&verified);
  if (!proof) return std::get<analysis::SemanticProofFailure>(verified);
  if (proof->allocated_bytes > budget.bytes)
    return SourceFailure{SourceError::kResourceLimit, {}};
  auto remaining = budget.bytes - proof->allocated_bytes;
  auto captures = ProveDefinedCaptures(body, *method, *proof, frame.registers,
      constants, {remaining, budget.work});
  auto* defined = std::get_if<analysis::DefinedCaptures>(&captures);
  if (!defined) return std::get<SourceFailure>(captures);
  if (defined->AllocatedBytes() > remaining || defined->work_units > budget.work)
    return SourceFailure{SourceError::kResourceLimit, {}};
  remaining -= defined->AllocatedBytes();
  budget.work -= defined->work_units;
  proof->allocated_bytes += defined->AllocatedBytes();
  proof->defined_captures = std::move(*defined);
  const auto checked = ValidateNamedScopes(
      body, frame.registers, &constants, remaining, budget.work);
  const auto* scopes = std::get_if<NamedScopes>(&checked);
  if (!scopes) {
    const auto& failure = std::get<BodyEmitFailure>(checked);
    return SourceFailure{failure.kind == BodyEmitError::kResourceLimit ||
                                 failure.kind == BodyEmitError::kWorkLimit
                             ? SourceError::kResourceLimit : SourceError::kConstant,
                         failure.offset};
  }
  if (scopes->AllocatedBytes() > remaining)
    return SourceFailure{SourceError::kResourceLimit, {}};
  remaining -= scopes->AllocatedBytes();
  auto recovered = analysis::BuildSemanticRegions(*method, remaining);
  auto* regions = std::get_if<analysis::SemanticRegionTree>(&recovered);
  if (!regions) return std::get<analysis::RegionFailure>(recovered);
  const auto rest = names.rest
      ? analysis::ProveRestPrologue(body, frame, names.rest->first_argument)
      : std::nullopt;
  if (names.rest && !rest)
    return SourceFailure{SourceError::kInvalidEffect, {}};
  auto lowered = BuildSourceAst(*method, *proof, std::move(*regions), remaining,
                                scopes->admitted, rest, derived);
  const auto* ast = std::get_if<SourceAst>(&lowered);
  if (!ast) return std::get<SourceFailure>(lowered);
  // This owner discharges ClosureWrites::kLanguage only by completing the
  // printer: every creation is retained, all children validate their stores,
  // and typed capture rebasing proves initialization at each creation. A
  // child failure discards the entire artifact. Do not publish the AST alone
  // as an unconditional proof, or infer immutable/non-undefined captures.
  if constexpr (Captures == analysis::CapturedValues::kReject) {
    auto printed = PrintSourceBody(*ast, *method, names, constants, output_bytes, proof);
    if (auto* source = std::get_if<PrintedSource>(&printed)) return std::move(*source);
    return std::get<SourceFailure>(printed);
  } else {
    auto printed = PrintSourceTemplate(*ast, *method, names, constants, output_bytes, proof);
    if (auto* source = std::get_if<PrintedTemplate>(&printed)) return std::move(*source);
    return std::get<SourceFailure>(printed);
  }
  } catch (const std::bad_alloc&) {
    return SourceFailure{SourceError::kResourceLimit, {}};
  } catch (const std::length_error&) {
    return SourceFailure{SourceError::kResourceLimit, {}};
  }
}
}  // namespace
SemanticBodyResult BuildSemanticBody(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes) {
  if (!body.tries.empty())
    return BuildExceptionBody(body, frame, names, constants, budget, output_bytes);
  return BuildBody<analysis::CapturedValues::kReject>(
      body, frame, names, constants, budget, output_bytes);
}
SemanticTemplateResult BuildSemanticTemplate(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    ScalarNames names, const MethodConstants& constants,
    analysis::SemanticBudget budget, std::size_t output_bytes) {
  if (!body.tries.empty())
    return BuildExceptionTemplate(body, frame, names, constants, budget, output_bytes);
  return BuildBody<analysis::CapturedValues::kLanguage>(
      body, frame, names, constants, budget, output_bytes);
}
}  // namespace abcd::source
