#ifndef ABCD_SOURCE_BRACE_PLAN_H_
#define ABCD_SOURCE_BRACE_PLAN_H_
#include "binding_initializer.h"
#include "loop_syntax.h"
#include "nullish_syntax.h"
#include "tree_checks.h"
namespace abcd::source {
enum class BraceReason : std::uint8_t {
  kRequired, kScope, kBinding, kLabel, kProjection, kDirective, kContent,
  kStatementList, kElseIf
};
struct BracePlan {
  std::vector<BraceReason> statements;
  std::size_t allocated_bytes=0, retained_bytes=0;
  std::uint64_t work=0;
};
[[nodiscard]] BracePlan BuildBracePlan(const FunctionTree&,const BindingInitializers&,
    const NullishSyntax&,const LoopSyntax&,TreeCheckBudget&);
[[nodiscard]] bool VerifyBracePlan(const FunctionTree&,const BindingInitializers&,
    const NullishSyntax&,const LoopSyntax&,const BracePlan&,TreeCheckBudget&);
}
#endif
