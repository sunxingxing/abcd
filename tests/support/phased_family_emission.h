#ifndef ABCD_TESTS_PHASED_FAMILY_EMISSION_H_
#define ABCD_TESTS_PHASED_FAMILY_EMISSION_H_

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>
#include <source_location>

#include "../../src/source/family_emission.h"
#include "../../src/source/selected_class_method.h"
#include "function_segments.h"
#include "abcd/source/module_enclosure.h"
#include "instruction_positions.h"
#include "function_segments.h"

namespace abcd::test {
// Exercise the actual record-to-common-family bridge, including retained
// segment coordinates. A direct family draft alone cannot expose old bridge
// callable restrictions or dropped receipts.
inline void CheckSelectedClassBridge(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::ClassMethodSites& classes,
    analysis::ClassSiteId site, source::RouteInputIdentity identity,
    std::u16string_view name, std::string_view expected) {
  const auto check = [](bool value, std::source_location at = std::source_location::current()) {
    if (!value) { std::cerr << at.file_name() << ':' << at.line() << '\n'; std::abort(); }
  };
  using namespace source;
  const auto* occurrence = classes.At(site);
  check(occurrence != nullptr);
  const abc::ClassProperty property{occurrence->placement, occurrence->key,
      abc::ClassMethod{occurrence->target, occurrence->kind, occurrence->length}};
  const auto target = ResolveClassMember(header, index, lookup, property, &roles);
  BoundSegmentSink sink;
  std::ostream output(&sink);
  CreationExpansion expansion(occurrence->creator, LocalLexicalBudget{}.bytes, kDefaultMethodWork);
  const ClassMethodSelection selection{classes, site, identity};
  try {
    const auto bytes = WriteSelectedClassMethod(selection, file, header, index,
        lookup, roles, property, target, name, expansion, output, {}, nullptr);
    check(bytes == expected.size() && sink.text == expected && sink.segments.size() == 1);
    const auto& segment = sink.segments.front();
    check(segment.begin.byte == 0 && segment.end.byte == bytes);
    const auto view = ViewFunctionSegment(segment);
    check(view.receipt.method == occurrence->target && !view.receipt.legacy_writer_used &&
        view.receipt.coverage == Coverage::kComplete && view.receipt.stage == SourceStage::kPrinted);
  } catch (const ClassFailure& error) {
    std::visit([](const auto& failure) {
      if constexpr (std::is_enum_v<std::decay_t<decltype(failure)>>)
        std::cerr << "selected class error " << static_cast<unsigned>(failure) << '\n';
      else if constexpr (std::is_same_v<std::decay_t<decltype(failure)>, RouteRefusal>)
        std::cerr << "selected family error " << static_cast<unsigned>(failure.failure.reason) << '\n';
    }, error.result);
    check(false);
  }
}
inline void CheckPhasedFamilyEmission(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& functions,
    const analysis::ClassMethodSites& classes, analysis::ClassSiteId site,
    source::RouteInputIdentity identity, std::string_view expected, const source::GlobalScope* scope = nullptr) {
  using namespace source;
  const auto check = [](bool ok) { if (!ok) std::abort(); };
  const FamilyDestination destination{Strictness::kStrict, scope, FunctionFragment::kClassMember};
  auto prepared = PreparedFamily::PrepareClass(file, header, index, lookup, roles,
      functions, classes, site, identity, destination);
  check(std::holds_alternative<PreparedFamily>(prepared));
  auto family = std::get<PreparedFamily>(std::move(prepared));
  check(family.Family().members.size() == 1 && family.Receipts().size() == 1);
  check(family.Receipts()[0].coverage == Coverage::kIncomplete &&
      family.Receipts()[0].stage == SourceStage::kTreeVerified);
  std::ostringstream denied;
  const auto misplaced = family.PrintDraft({Strictness::kStrict, nullptr}, denied);
  check(std::get<FamilyPrepareError>(std::get<FamilyPrepareFailure>(misplaced).cause) == FamilyPrepareError::kPlacement);
  check(denied.str().empty());
  if (scope && scope->module_enclosure) {
    const auto refuse = [&](const GlobalScope* target) {
      std::ostringstream rejected;
      const auto result = family.PrintDraft({Strictness::kStrict, target, FunctionFragment::kClassMember}, rejected);
      check(std::holds_alternative<FamilyPrepareFailure>(result) && rejected.str().empty());
    };
    refuse(nullptr);
    GlobalScope names_only{};
    names_only.import_bindings = scope->module_enclosure->Names();
    names_only.import_names = scope->module_enclosure->SortedNames();
    refuse(&names_only);
    GlobalScope shadowed = *scope;
    shadowed.class_binding = scope->module_enclosure->Names().front();
    refuse(&shadowed);
    auto wrong_owner = scope->module_enclosure->Owner(); ++wrong_owner.value;
    const auto foreign = std::get<ModuleEnclosure>(ModuleEnclosure::Build(file, header,
        scope->module_enclosure->Context(), identity, wrong_owner));
    GlobalScope wrong = *scope; wrong.module_enclosure = &foreign;
    refuse(&wrong);
    auto forged = family.Family();
    check(forged.members[0].record.has_value());
    ++forged.members[0].record->value;
    check(std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(forged, scope)).kind == FamilyEnclosureError::kModuleBinding);
    forged.members[0].record.reset();
    check(std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(forged, scope)).kind == FamilyEnclosureError::kMissingModuleContext);
  }
  std::ostringstream output;
  auto published = FamilyEmission::Write(std::move(family), destination, output, {});
  check(std::holds_alternative<FamilyEmission>(published));
  const auto& emission = std::get<FamilyEmission>(published);
  check(output.str() == expected);
  check(emission.Receipts().size() == 1 && emission.Receipts()[0].coverage == Coverage::kComplete &&
      emission.Receipts()[0].stage == SourceStage::kPrinted && !ValidateRouteReceipt(emission.Receipts()[0]));
  const auto& member = emission.Family().Family().members[0];
  if (scope && scope->module_enclosure) {
    const auto& binding = emission.Positions().members[0].module_binding;
    check(binding && binding->record == scope->module_enclosure->Context().record() &&
        binding->owner == scope->module_enclosure->Owner());
  }
  check(member.sources.has_value() && member.abi.has_value());
  CheckInstructionPositions(member.tree, *member.sources, emission.Positions().members[0], emission.Positions().functions[0]);
  CheckFunctionTransport(std::get<FamilyEmission>(std::move(published)), output.str());
  const auto limited = PreparedFamily::PrepareClass(file, header, index, lookup, roles,
      functions, classes, site, identity, destination, {0, 0});
  check(std::get<FamilyPrepareError>(std::get<FamilyPrepareFailure>(limited).cause) == FamilyPrepareError::kResourceLimit);
  auto old = identity; old.contract = SourceContract::kLanguageAndHostV1;
  check(std::holds_alternative<FamilyPrepareFailure>(PreparedFamily::PrepareClass(
      file, header, index, lookup, roles, functions, classes, site, old, destination)));
}
}  // namespace abcd::test
#endif
