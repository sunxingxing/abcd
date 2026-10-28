#ifndef ABCD_SOURCE_CLASS_INITIALIZERS_H_
#define ABCD_SOURCE_CLASS_INITIALIZERS_H_
#include "abcd/analysis/value_ir.h"
#include <span>
#include "class_definitions.h"
#include "class_initializer_call.h"
#include "class_prototype.h"
namespace abcd::source {
class ClassSelfOrder;
// es2abc instance fields: a zero-length DEFINEMETHOD whose home object is the
// class's own prototype read, stored once into a lexical slot. The class
// constructor loads that slot and CALLINITs it on its instance. This records
// the creator occurrence only; the slot's privacy, the constructor event and
// the body's field definitions are separate obligations.
struct ClassInstanceInitializer {
  analysis::OpId creation;
  analysis::OpId prototype;
  analysis::OpId method;
  analysis::OpId store;
  analysis::LexicalAddress slot;
  // The home object is the prototype read itself; otherwise it is another
  // member function, observable only through super, which the body must lack.
  bool prototype_home = true;
  bool operator==(const ClassInstanceInitializer& other) const {
    return creation == other.creation && prototype == other.prototype && method == other.method &&
        prototype_home == other.prototype_home &&
        store == other.store && slot.depth.value == other.slot.depth.value && slot.slot.value == other.slot.slot.value;
  }
};
struct ClassInitializerCalls {
  analysis::IrRevision revision;
  std::vector<ClassInitializerCall> calls;
  std::vector<ClassInstanceInitializer> instances;  // Sorted by method.
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ClassInitializerError : std::uint8_t { kInput, kFlow, kResourceLimit, kWorkLimit };
// Fresh simplified normal IR. Identify a nonescaping zero-argument method's
// single call on its own class home object. Fresh common SSA availability proves
// creation/home/callee order across arbitrary CFG splits; block layout cannot
// impose a second admission condition. Names are never consulted.
// Cost is the common flow proof plus O(operations + uses), with def/use indexes.
// This establishes invocation identity only: moving the call into class syntax
// still needs body/return/field semantics and a class escape/effect corridor.
[[nodiscard]] std::variant<ClassInitializerCalls, ClassInitializerError> FindClassInitializerCalls(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
// Conditional projection: each property read in prototypes must separately be
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
    std::span<const ClassDefinitionSite> = {}, const ClassSelfOrder* = nullptr,
    std::span<const ClassPrototypeRead> = {});
}
#endif
