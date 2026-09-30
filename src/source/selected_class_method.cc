#include "selected_class_method.h"

#include "abcd/source/class_method.h"
#include "abcd/source/semantic_source.h"
#include "capture_stream.h"
#include "family_route_failure.h"

namespace abcd::source {
std::size_t WriteSelectedClassMethod(
    const ClassMethodSelection& selection, const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const abc::ClassProperty& property, const ClassMemberTarget& target,
    std::u16string_view key, CreationExpansion& expansion,
    std::ostream& output, LocalLexicalBudget budget, const GlobalScope* scope) {
  auto* sink = dynamic_cast<CaptureBuffer*>(output.rdbuf());
  GeneratedPosition begin;
  if (!sink || !sink->Position(begin)) throw ClassFailure{EmitError::kInvalidExpression};
  const auto* encoded = std::get_if<abc::ClassMethod>(&property.value);
  const auto* site = selection.sites.At(selection.site);
  if (!encoded || !site || target.position.value >= index.methods.size() ||
      encoded->target != site->target || encoded->kind != site->kind ||
      encoded->length != site->length || property.placement != site->placement ||
      index.methods[target.position.value].offset != site->target)
    throw ClassFailure{FunctionEmitError::kCreation};
  if (target.attributes.sendable) throw ClassFailure{FunctionEmitError::kSendable};
  // This bridge does not normalize async bodies. The actual creation context
  // and PreparedFamily own the shared async protocol checks and native syntax.
  // Do not retain the earlier NONE-only restriction after that integration.
  if ((target.attributes.kind != abc::FunctionKind::kNone &&
       target.attributes.kind != abc::FunctionKind::kAsync) ||
      site->kind != abc::ClassMethodKind::kOrdinary)
    throw ClassFailure{FunctionEmitError::kKind};
  ExpansionFrame admission(expansion, site->target, budget);
  if (const auto error = admission.error())
    throw ClassFailure{*error == ExpansionError::kResourceLimit
        ? CreatedClassError::kResourceLimit : CreatedClassError::kRecursiveExpansion};
  // Selection names one occurrence per invocation. Index child creations once,
  // not per family member. A refused selected route never retries legacy bodies.
  constexpr std::size_t kStageShares = 2;
  analysis::SemanticBudget preparation{budget.bytes / kStageShares, budget.work / kStageShares};
  const auto printing = analysis::SemanticBudget{budget.bytes - preparation.bytes,
                                                budget.work - preparation.work};
  auto indexed = analysis::FunctionCreationSites::Build(file, header, index, lookup, preparation);
  const auto* functions = std::get_if<analysis::FunctionCreationSites>(&indexed);
  if (!functions) throw ClassFailure{RouteRefusal{FamilyRouteFailure(std::get<analysis::FunctionSiteFailure>(indexed))}};
  if (functions->AllocatedBytes() > preparation.bytes)
    throw ClassFailure{RouteRefusal{{RouteFailureClass::kMemoryLimit, ValueRouteError::kContextStage, {}}}};
  if (functions->Work() > preparation.work)
    throw ClassFailure{RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}}};
  preparation.bytes -= functions->AllocatedBytes(); preparation.work -= functions->Work();
  const FamilyDestination destination{Strictness::kStrict, scope, FunctionFragment::kClassMember};
  auto prepared = PreparedFamily::PrepareClass(file, header, index, lookup, roles,
      *functions, selection.sites, selection.site, selection.identity, destination, preparation);
  auto* family = std::get_if<PreparedFamily>(&prepared);
  if (!family) throw ClassFailure{RouteRefusal{FamilyRouteFailure(std::get<FamilyPrepareFailure>(prepared))}};
  if (family->Name() != key) throw ClassFailure{FunctionEmitError::kName};
  auto printed = FamilyEmission::Write(std::move(*family), destination, output, {begin.byte}, budget.output, printing);
  auto* emission = std::get_if<FamilyEmission>(&printed);
  if (!emission) throw ClassFailure{RouteRefusal{FamilyRouteFailure(std::get<FamilyEmitFailure>(printed))}};
  GeneratedPosition end;
  if (!sink->Position(end)) throw ClassFailure{EmitError::kInvalidExpression};
  const auto bytes = emission->Positions().bytes;
  if (!sink->RetainFunction({std::make_shared<const FamilyEmission>(std::move(*emission)), begin, end}))
    throw ClassFailure{EmitError::kInvalidExpression};
  return bytes;
}
}  // namespace abcd::source
