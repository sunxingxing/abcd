#ifndef ABCD_TESTS_PHASED_FAMILY_CHECKS_H_
#define ABCD_TESTS_PHASED_FAMILY_CHECKS_H_

#include "../../src/source/family_tree.h"

namespace abcd::test {
// Reread real decoded inputs: never reuse a family consumed by construction.
inline bool CheckPhasedFamilyRefusals(const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& sites, const source::FunctionContext& context,
    const source::RecordModuleContext* modules, source::ModuleOwnerId owner,
    analysis::SemanticBudget budget) {
  using namespace source;
  const auto read = [&](const FunctionContext* root) {
    return ReadFunctionFamily(file, header, index, lookup, roles, sites,
        context.Position(), context.Identity(), budget, modules, root, owner);
  };
  const auto ownerless = read(nullptr);
  const auto* rejected = std::get_if<FamilyFileFailure>(&ownerless);
  if (!rejected || std::get_if<FamilyFileError>(&rejected->cause) == nullptr ||
      std::get<FamilyFileError>(rejected->cause) != FamilyFileError::kOccurrence) return false;
  enum class Mutation { kMissingLookup, kMissingEdge, kWrongFlow };
  for (const auto mutation : {Mutation::kMissingLookup, Mutation::kMissingEdge, Mutation::kWrongFlow}) {
    auto loaded = read(&context);
    auto* family = std::get_if<FileFamily>(&loaded);
    if (!family || family->members.size() < 2 || !family->members[1].incoming) return false;
    if (mutation == Mutation::kMissingEdge) family->members[1].incoming.reset();
    if (mutation == Mutation::kWrongFlow) family->members[1].incoming->phase_operation.reset();
    const auto result = BuildFileFamilyTrees(file, header, index, std::move(*family), context,
        budget, modules, owner, mutation == Mutation::kMissingLookup ? nullptr : &lookup);
    const auto* failure = std::get_if<FamilyTreeFailure>(&result);
    if (!failure) return false;
    const auto* error = std::get_if<FamilyTreeError>(&failure->cause);
    const auto expected = mutation == Mutation::kMissingLookup ? FamilyTreeError::kContext : FamilyTreeError::kParent;
    if (!error || *error != expected) return false;
  }
  return true;
}
}  // namespace abcd::test
#endif
