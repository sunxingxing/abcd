#include "abcd/source/semantic_entry.h"

#include <new>
#include <stdexcept>

#include "abcd/source/capture_preservation.h"
#include "abcd/source/named_scope.h"

namespace abcd::source {
SemanticBodyResult BuildEntrySource(
    const abc::MethodBody& body, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, ModuleExports* exports,
    analysis::SemanticBudget budget, std::size_t output_bytes) {
  const SourceFailure resource{SourceError::kResourceLimit, {}};
  try {
    if (method.allocated_bytes > budget.bytes) return resource;
    const auto preserved = ProveCapturePreservation(body, constants,
        constants.leaf_methods, {budget.bytes - method.allocated_bytes, budget.work});
    const auto* sites = std::get_if<CapturePreservations>(&preserved);
    if (!sites) return std::get<SourceFailure>(preserved);
    if (sites->AllocatedBytes() > budget.bytes - method.allocated_bytes ||
        sites->work_units > budget.work) return resource;
    const auto proof_bytes = budget.bytes - sites->AllocatedBytes();
    const auto proof = analysis::VerifySemanticSource(body, method,
        frame.registers, frame.locals, {proof_bytes, budget.work - sites->work_units},
        analysis::CapturedValues::kReject, {}, analysis::ClosureWrites::kUnknown,
        sites->sites);
    const auto* verified = std::get_if<analysis::SemanticProof>(&proof);
    if (!verified) return std::get<analysis::SemanticProofFailure>(proof);
    if (verified->allocated_bytes > proof_bytes) return resource;
    const auto remaining = proof_bytes - verified->allocated_bytes;
    auto recovered = analysis::BuildSemanticRegions(method, remaining);
    auto* regions = std::get_if<analysis::SemanticRegionTree>(&recovered);
    if (!regions) return std::get<analysis::RegionFailure>(recovered);
    if (regions->allocated_bytes > remaining) return resource;
    const auto checked = ValidateNamedScopes(body, frame.registers, &constants,
        remaining - regions->allocated_bytes, budget.work);
    const auto* scopes = std::get_if<NamedScopes>(&checked);
    if (!scopes) {
      const auto& failure = std::get<BodyEmitFailure>(checked);
      return SourceFailure{
          failure.kind == BodyEmitError::kResourceLimit ||
                  failure.kind == BodyEmitError::kWorkLimit
              ? SourceError::kResourceLimit : SourceError::kConstant,
          failure.offset};
    }
    if (scopes->AllocatedBytes() > remaining - regions->allocated_bytes)
      return resource;
    const auto lowered = BuildSourceAst(method, *verified, std::move(*regions),
        remaining - scopes->AllocatedBytes(), scopes->admitted);
    const auto* ast = std::get_if<SourceAst>(&lowered);
    if (!ast) return std::get<SourceFailure>(lowered);
    auto rendered = PrintSourceAst(*ast, method, frame, form, name, constants,
                                   output_bytes, verified, exports);
    if (auto* source = std::get_if<PrintedSource>(&rendered)) return std::move(*source);
    return std::get<SourceFailure>(rendered);
  } catch (const std::bad_alloc&) {
    return resource;
  } catch (const std::length_error&) {
    return resource;
  }
}
}  // namespace abcd::source
