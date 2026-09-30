#ifndef ABCD_SOURCE_CLASS_INITIALIZERS_H_
#define ABCD_SOURCE_CLASS_INITIALIZERS_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
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
}
#endif
