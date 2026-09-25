#ifndef ABCD_ANALYSIS_VALUE_REWRITE_H_
#define ABCD_ANALYSIS_VALUE_REWRITE_H_

#include <span>
#include "abcd/analysis/value_ir.h"

namespace abcd::analysis {
enum class ValueRewriteError : std::uint8_t {
  kRevision, kShape, kDefinition, kAlias, kUnanchored, kOperand,
  kEffect, kOrigin, kWorkLimit, kResourceLimit
};
struct ValueRewriteProof {
  IrRevision input_revision;
  CfgRevision cfg_revision;
  std::uint64_t work;
};
// Independent precommit checker, not a replay of the optimizer. Both graphs
// have already passed normal-flow verification. Revisions remain unchanged until
// commit. Every alias needs an identity equation AND a path to its fixed root;
// cyclic equations alone cannot justify replacing an uninitialized phi ring.
[[nodiscard]] std::variant<ValueRewriteProof, ValueRewriteError> VerifyValueRewrite(
    const ValueProgram& before, const ValueProgram& after,
    std::span<const ValueId> representatives, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
