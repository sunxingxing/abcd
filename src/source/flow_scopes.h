#ifndef ABCD_SOURCE_FLOW_SCOPES_H_
#define ABCD_SOURCE_FLOW_SCOPES_H_
#include "tree_checks.h"
#include <memory>
namespace abcd::source {
struct FlowPoint;
struct CompletionOnlyScope {};
struct IncomingScope {};
struct UnmodeledScope {};
using FlowScopeContext=std::variant<CompletionOnlyScope,IncomingScope,UnmodeledScope,ScopeId>;
struct FlowScopeEvent {
  FlowScopeContext before=CompletionOnlyScope{},after=CompletionOnlyScope{};
};
struct ScopePosition {std::size_t value=0;auto operator<=>(const ScopePosition&)const=default;};
struct FlowScopeRegion {ScopePosition first,last;};
enum class FlowScopePhase : std::uint8_t {kBefore,kAfter};
enum class FlowUnitPresence : std::uint8_t {kActive,kInactive,kUnmodeled,kCompletionOnly};
enum class FlowUnitTransfer : std::uint8_t {kPreserved,kEnded,kUnmodeled};
// Immutable per-projection lexical activation contexts. Euler intervals index
// containment, never runtime cell equality. Unknown cleanup must retain value
// demand while invalidating substitutions, rather than pretend a definite exit.
struct FlowScopes {
  std::vector<FlowScopeRegion> regions;
  std::vector<FlowScopeEvent> events;
  [[nodiscard]] FlowUnitPresence Presence(ScopeId,FlowPoint,FlowScopePhase) const;
  [[nodiscard]] FlowUnitTransfer Transfer(ScopeId,FlowPoint,FlowPoint) const;
};
[[nodiscard]] std::shared_ptr<FlowScopes> MakeFlowScopes(const FunctionTree&,TreeCheckBudget&);
}
#endif
