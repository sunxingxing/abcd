#ifndef ABCD_TESTS_EXCEPTION_FAMILY_EMIT_H_
#define ABCD_TESTS_EXCEPTION_FAMILY_EMIT_H_

#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "../../src/source/family_emission.h"
#include "abcd/source/module_enclosure.h"
#include "../../tools/tree_position_json.h"
#include "../support/instruction_positions.h"

namespace abcd::test {
inline int EmitExceptionFamily(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::ClassMethodSites& classes,
    analysis::ClassSiteId site, const source::ModuleEnclosure& enclosure,
    analysis::SemanticBudget budget) {
  using namespace source;
  const auto work_limit = budget.work;
  const auto indexed = analysis::FunctionCreationSites::Build(file, header, index, lookup, budget);
  const auto* functions = std::get_if<analysis::FunctionCreationSites>(&indexed);
  if (!functions || functions->AllocatedBytes() > budget.bytes || functions->Work() > budget.work) return 1;
  budget.bytes -= functions->AllocatedBytes(); budget.work -= functions->Work();
  GlobalScope scope{}; scope.module_enclosure = &enclosure;
  const FamilyDestination destination{Strictness::kStrict, &scope, FunctionFragment::kClassMember};
  auto ready = PreparedFamily::PrepareClass(file, header, index, lookup, roles,
      *functions, classes, site, enclosure.Identity(), destination, budget);
  if (!std::holds_alternative<PreparedFamily>(ready)) {
    std::cerr << "family preparation failure " << std::get<FamilyPrepareFailure>(ready).cause.index() << '\n'; return 1;
  }
  std::ostringstream source, imports;
  if (!std::holds_alternative<std::size_t>(enclosure.WriteImports(file, header, imports))) return 1;
  auto printed = FamilyEmission::Write(std::get<PreparedFamily>(std::move(ready)), destination, source, {}, {}, budget);
  const auto* emission = std::get_if<FamilyEmission>(&printed);
  if (!emission) return 1;
  nlohmann::json members = nlohmann::json::array();
  for (std::size_t i = 0; i < emission->Receipts().size(); ++i) {
    const auto& receipt = emission->Receipts()[i];
    if (!receipt.method || receipt.coverage != Coverage::kComplete || ValidateRouteReceipt(receipt)) return 1;
    const auto& member = emission->Family().Family().members[i];
    const auto& positions = emission->Positions().members[i];
    CheckInstructionPositions(member.tree, *member.sources, positions, emission->Positions().functions[i]);
    if (!member.tree.modules.empty() && (!positions.module_binding ||
        positions.module_binding->record != enclosure.Context().record() ||
        positions.module_binding->owner != enclosure.Owner())) return 1;
    std::ostringstream mapping;
    tools::WriteTreePositions(mapping, member.tree, positions, &*member.sources);
    members.push_back({{"method", receipt.method->value}, {"positions", nlohmann::json::parse(mapping.str())}});
  }
  std::cout << nlohmann::json{{"fragment_complete", true}, {"record_complete", false},
      {"source", source.str()}, {"imports", imports.str()}, {"members", members},
      {"work_limit", work_limit}, {"positions", members.at(emission->Family().Family().root.value).at("positions")}} << '\n';
  return file.CheckUnchanged() ? 1 : 0;
}
}  // namespace abcd::test
#endif
