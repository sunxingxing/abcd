#include "selected_function.h"

#include "abcd/source/semantic_source.h"
#include "capture_stream.h"
#include "family_emission.h"
#include "family_route_failure.h"

namespace abcd::source {
CreatedFunctionResult WriteSelectedFunction(
    const FunctionCreationSelection& selection, const analysis::FunctionCreation& creation,
    abc::CodeOffset pc, const MethodConstants& constants, CreationExpansion& expansion,
    std::ostream& output, LocalLexicalBudget budget) {
  const auto* site = selection.sites.At(selection.site);
  if (!site || site->creator != constants.method || site->pc != pc ||
      site->creation.kind != creation.kind || site->creation.method != creation.method ||
      site->creation.length.value != creation.length.value ||
      site->creation.home_object != creation.home_object || site->creation.cache_slot != creation.cache_slot)
    return FunctionEmitError::kCreation;
  if (!constants.roles || !constants.leaf_methods) return CreatedFunctionError::kRole;
  auto* sink = dynamic_cast<CaptureBuffer*>(output.rdbuf());
  GeneratedPosition begin;
  if (!sink || !sink->Position(begin)) return EmitError::kInvalidExpression;
  ExpansionFrame admission(expansion, site->target, budget);
  if (const auto error = admission.error()) return *error == ExpansionError::kCycle
      ? CreatedFunctionError::kCreationCycle : CreatedFunctionError::kResourceLimit;
  // The real generated record is a strict module. Its complete binding context
  // is borrowed only for this emission; root captures still require proof and
  // are refused by PrepareCreated. Never retry the legacy body on refusal.
  constexpr std::size_t kStageShares = 2;
  const analysis::SemanticBudget preparation{budget.bytes / kStageShares, budget.work / kStageShares};
  const analysis::SemanticBudget printing{budget.bytes - preparation.bytes, budget.work - preparation.work};
  const FamilyDestination destination{Strictness::kStrict,
      constants.global_scope ? &*constants.global_scope : nullptr};
  auto prepared = PreparedFamily::PrepareCreated(constants.file, constants.header, constants.index,
      *constants.leaf_methods, *constants.roles, selection.sites, selection.site,
      selection.identity, destination, preparation);
  auto* family = std::get_if<PreparedFamily>(&prepared);
  if (!family) return RouteRefusal{FamilyRouteFailure(std::get<FamilyPrepareFailure>(prepared))};
  auto printed = FamilyEmission::Write(std::move(*family), destination, output,
      {begin.byte}, budget.output, printing);
  auto* emission = std::get_if<FamilyEmission>(&printed);
  if (!emission) return RouteRefusal{FamilyRouteFailure(std::get<FamilyEmitFailure>(printed))};
  GeneratedPosition end;
  if (!sink->Position(end)) return EmitError::kInvalidExpression;
  const auto bytes = emission->Positions().bytes;
  if (!sink->RetainFunction({std::make_shared<const FamilyEmission>(std::move(*emission)), begin, end}))
    return EmitError::kInvalidExpression;
  return bytes;
}
}  // namespace abcd::source
