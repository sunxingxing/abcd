#ifndef ABCD_SOURCE_CLASS_ACCESSORS_H_
#define ABCD_SOURCE_CLASS_ACCESSORS_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
struct ClassAccessorSite {
  analysis::OpId creation;
  analysis::OpId definition;
  analysis::ValueId home;
  std::optional<analysis::OpId> prototype;
  abc::PoolIndex key;
  std::optional<analysis::OpId> getter;
  std::optional<analysis::OpId> setter;
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
}
#endif
