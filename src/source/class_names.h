#ifndef ABCD_SOURCE_CLASS_NAMES_H_
#define ABCD_SOURCE_CLASS_NAMES_H_
#include "class_tree.h"
#include "tree_structure.h"
#include "source_variables.h"
#include <span>
namespace abcd::source {
struct ClassNames {
  struct Entry { const ClassTree* definition; bool named; };
  struct Use { bool named; std::uint64_t work; };
  std::vector<Entry> entries;
  // Owned descendant plans used for this exact immutable printing projection.
  // Sorted by tree identity; supplied caller plans remain borrowed by caller.
  std::vector<SourceVariables> prepared;
  // Naming also depends on the caller's immutable external binding context.
  const GlobalScope* scope=nullptr;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] Use Lookup(const ClassTree&) const;
  struct Variables {const SourceVariables* plan;std::uint64_t work;};
  [[nodiscard]] Variables Lookup(const FunctionTree&) const;
};
// Plan added class-self scopes over unchanged, checked source ownership. Native
// naming is safe only if no actual reference would resolve to the added name.
// Walk lexical class scopes, captures and heritage once; never test a spelling
// against every body independently. Missing bodies remain explicit unknowns.
[[nodiscard]] std::variant<ClassNames, TreeStructureError> PlanClassNames(
    const ConstructedFamily&, const GlobalScope*, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<ClassNames, TreeStructureError> PlanClassNames(
    const FunctionTree&, const GlobalScope*, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<ClassNames, TreeStructureError> PlanClassNames(
    const ClassTree&, const GlobalScope*, analysis::SemanticBudget = {});
// Printing changes expression ownership through proven snapshot/store
// projections. Analyze those destinations before introducing class-self scopes.
// Canonical overloads above remain available for lexical ownership analysis.
// Supplied plans belong to these exact unchanged tree objects and outlive this
// call. They are borrowed without charging or recomputing caller-owned facts.
[[nodiscard]] std::variant<ClassNames, TreeStructureError> PlanPrintedClassNames(
    const ConstructedFamily&, const GlobalScope*, analysis::SemanticBudget = {},
    std::span<const SourceVariables> = {});
[[nodiscard]] std::variant<ClassNames, TreeStructureError> PlanPrintedClassNames(
    const FunctionTree&, const GlobalScope*, analysis::SemanticBudget = {}, std::span<const SourceVariables> = {});
[[nodiscard]] std::variant<ClassNames, TreeStructureError> PlanPrintedClassNames(
    const ClassTree&, const GlobalScope*, analysis::SemanticBudget = {}, std::span<const SourceVariables> = {});
}
#endif
