#ifndef ABCD_SOURCE_CLASS_DEFINITIONS_H_
#define ABCD_SOURCE_CLASS_DEFINITIONS_H_
#include "abcd/analysis/value_ir.h"
#include "abcd/abc/class_buffer.h"
#include <array>
#include <span>
namespace abcd::source {
struct ClassAccessorDefinition {
  std::optional<analysis::OpId> getter;
  std::optional<analysis::OpId> setter;
  bool operator==(const ClassAccessorDefinition&) const = default;
};
struct ClassMethodDefinition {
  analysis::OpId method;
  bool operator==(const ClassMethodDefinition&) const = default;
};
struct ClassDefinitionMethod {
  std::optional<analysis::OpId> operation;
  abc::ClassMethodKind kind;
};
struct ClassDefinitionSite {
  analysis::OpId creation;
  analysis::OpId definition;
  analysis::ValueId home;
  std::optional<analysis::OpId> prototype;
  abc::PoolIndex key;
  std::variant<ClassAccessorDefinition, ClassMethodDefinition> members;
  std::array<ClassDefinitionMethod, 2> Methods() const {
    if (const auto* accessor = std::get_if<ClassAccessorDefinition>(&members))
      return {{{accessor->getter, abc::ClassMethodKind::kGetter}, {accessor->setter, abc::ClassMethodKind::kSetter}}};
    return {{{std::get<ClassMethodDefinition>(members).method, abc::ClassMethodKind::kOrdinary}, {{}, abc::ClassMethodKind::kOrdinary}}};
  }
  std::optional<analysis::OpId> Method(abc::ClassMethodKind kind) const {
    for (const auto& method : Methods()) if (method.kind == kind && method.operation) return method.operation;
    return {};
  }
  bool operator==(const ClassDefinitionSite&) const = default;
};
struct ClassDefinitionSites {
  analysis::IrRevision revision;
  std::vector<ClassDefinitionSite> sites;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ClassDefinitionError : std::uint8_t { kInput, kFlow, kResourceLimit, kWorkLimit };
// Fresh simplified normal IR. Discover exact class/home/function occurrences
// without consulting method names. O(ops + uses). This does not move effects:
// prototype names, descriptor semantics, class escape corridors, and actual
// member bodies still need checking before projection into native class syntax.
[[nodiscard]] std::variant<ClassDefinitionSites, ClassDefinitionError> FindClassDefinitions(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
struct ClassDefinitionProjection {
  std::vector<std::optional<analysis::OpId>> owners;
  std::vector<analysis::OpId> prototypes;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Conditional movement into the class creation. Rechecks occurrences and proves
// the entire class-to-last-definition corridor unobserved. Prototype spellings,
// descriptor keys and prepared bodies remain source-level obligations. No live
// definition-result alias or escaping prototype is silently discarded.
// O(ops + uses + sites), including repeated definitions on the same class.
[[nodiscard]] std::variant<ClassDefinitionProjection, ClassDefinitionError> ProjectClassDefinitions(
    const analysis::ValueProgram&, std::span<const ClassDefinitionSite>, analysis::SemanticBudget = {});
}
#endif
