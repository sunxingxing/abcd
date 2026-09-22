#ifndef ABCD_SOURCE_ACYCLIC_BODY_H_
#define ABCD_SOURCE_ACYCLIC_BODY_H_

#include "abcd/analysis/accumulator_flow.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
enum class AcyclicError { kResourceLimit, kNestingLimit };
struct AcyclicBudget {
  std::size_t bytes = 64 * 1024 * 1024;
  std::size_t max_nesting = 128;
  EmitBudget output;
};
using AcyclicResult =
    std::variant<std::size_t, BodyEmitFailure, analysis::FlowFailure,
                 analysis::AccumulatorFailure, AcyclicError>;

// Emits an acyclic function body using lexical label blocks and conditional
// breaks, not an opcode dispatcher. Register/name/constant contracts match
// WriteLinearBody. Validated body required. Exceptions, resumes and loops are
// rejected; normal-unreachable blocks are omitted only in this exception-free
// path. No expression/code duplication at joins. Labels have their own
// namespace. Graph + analysis are bounded by bytes; caller also admits body,
// transient string and output storage. Excessive JS nesting is an explicit
// failure. Preflight prevents partial semantic/budget output; IO/file changes
// still require atomic publication. Does not infer wrappers, bindings or
// modules.
[[nodiscard]] AcyclicResult WriteAcyclicBody(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    AcyclicBudget budget = {}, const MethodConstants* constants = nullptr);

// Same contracts and resource budgets, but permits single-block self loops.
// Removing self edges must leave a DAG. Emits while/continue/labeled break,
// never a program-counter dispatcher; multi-block cycles remain unsupported.
[[nodiscard]] AcyclicResult WriteBlockLoopBody(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    AcyclicBudget budget = {}, const MethodConstants* constants = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_ACYCLIC_BODY_H_
