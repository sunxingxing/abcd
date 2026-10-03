#ifndef ABCD_SOURCE_CLASS_INITIALIZERS_H_
#define ABCD_SOURCE_CLASS_INITIALIZERS_H_
#include "abcd/analysis/value_ir.h"
#include <span>
#include "class_definitions.h"
namespace abcd::source {
class ClassSelfOrder;
struct ClassInitializerCall {
  analysis::OpId creation;
  analysis::OpId method;
  analysis::OpId call;
  analysis::ValueId receiver;
  bool operator==(const ClassInitializerCall&) const = default;
};
struct ClassInitializerCalls {
  analysis::IrRevision revision;
  std::vector<ClassInitializerCall> calls;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ClassInitializerError : std::uint8_t { kInput, kFlow, kResourceLimit, kWorkLimit };
// Fresh simplified normal IR. Identify a nonescaping zero-argument method's
// single call on its own class home object, in original order and block.
// O(operations + uses), with direct def/use indexes. Names are never consulted.
// This establishes invocation identity only: moving the call into class syntax
// still needs body/return/field semantics and a class escape/effect corridor.
[[nodiscard]] std::variant<ClassInitializerCalls, ClassInitializerError> FindClassInitializerCalls(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
// Conditional projection: each named read in prototypes must separately be
// resolved to the builtin "prototype" data property by the source adapter.
// Body recovery must bind the same method occurrence into its class tree.
// Prototype reads with surviving later uses remain ordinary operations; this
// list also carries their spelling obligation. Initializer/accessor interleaving
// within one class remains rejected until their common ordering is proved.
struct ClassProjection {
  std::vector<std::optional<analysis::OpId>> owners;
  std::vector<analysis::OpId> prototypes;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<ClassProjection, ClassInitializerError> ProjectClassMembers(
    const analysis::ValueProgram&, std::span<const ClassInitializerCall>, analysis::SemanticBudget = {},
    std::span<const ClassDefinitionSite> = {}, const ClassSelfOrder* = nullptr);
}
#endif
