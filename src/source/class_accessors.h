#ifndef ABCD_SOURCE_CLASS_ACCESSORS_H_
#define ABCD_SOURCE_CLASS_ACCESSORS_H_
#include "abcd/analysis/value_ir.h"
#include <span>
namespace abcd::source {
struct ClassAccessorSite {
  analysis::OpId creation;
  analysis::OpId definition;
  analysis::ValueId home;
  std::optional<analysis::OpId> prototype;
  abc::PoolIndex key;
  std::optional<analysis::OpId> getter;
  std::optional<analysis::OpId> setter;
  bool operator==(const ClassAccessorSite&) const = default;
};
struct ClassAccessorSites {
  analysis::IrRevision revision;
  std::vector<ClassAccessorSite> sites;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ClassAccessorError : std::uint8_t { kInput, kFlow, kResourceLimit, kWorkLimit };
// Fresh simplified normal IR. Discover exact class/home/function occurrences
// without consulting method names. O(ops + uses). This does not move effects:
// prototype names, descriptor semantics, class escape corridors, and actual
// accessor bodies still need checking before projection into native class syntax.
[[nodiscard]] std::variant<ClassAccessorSites, ClassAccessorError> FindClassAccessors(
    const analysis::ValueProgram&, analysis::SemanticBudget = {});
struct ClassAccessorProjection {
  std::vector<std::optional<analysis::OpId>> owners;
  std::vector<analysis::OpId> prototypes;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Conditional movement into the class creation. Rechecks occurrences and proves
// the entire class-to-last-definition corridor unobserved. Prototype spellings,
// descriptor keys and prepared bodies remain source-level obligations. No live
// definition-result alias or escaping prototype is silently discarded.
// O(ops + uses + sites), including repeated accessors on the same class.
[[nodiscard]] std::variant<ClassAccessorProjection, ClassAccessorError> ProjectClassAccessors(
    const analysis::ValueProgram&, std::span<const ClassAccessorSite>, analysis::SemanticBudget = {});
}
#endif
