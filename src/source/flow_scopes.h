#ifndef ABCD_SOURCE_FLOW_SCOPES_H_
#define ABCD_SOURCE_FLOW_SCOPES_H_
#include "tree_checks.h"
#include <memory>
namespace abcd::source {
struct FlowPoint;
struct CompletionOnlyScope {auto operator<=>(const CompletionOnlyScope&)const=default;};
struct IncomingScope {auto operator<=>(const IncomingScope&)const=default;};
struct UnmodeledScope {auto operator<=>(const UnmodeledScope&)const=default;};
// Cleanup may lose knowledge of nested cells while the invocation's body
// environment remains live. Keep that guaranteed ancestry separate from the
// unknown completion/descendant state.
struct EnclosingScope {ScopeId scope;auto operator<=>(const EnclosingScope&)const=default;};
using FlowScopeContext=std::variant<CompletionOnlyScope,IncomingScope,UnmodeledScope,ScopeId,EnclosingScope>;
struct FlowScopeEvent {
  FlowScopeContext before=CompletionOnlyScope{},after=CompletionOnlyScope{};
};
// Native lexical environments are source-created allocation sites, not
// canonical AST ScopeIds. The common forest owns their nesting and point
// contexts; a site never identifies the same dynamic cell across entries.
struct ExitedLoopScope {StmtId loop;auto operator<=>(const ExitedLoopScope&)const=default;};
// A known ancestor is a guarantee, not proof that unrelated pending source
// environments have already ended. Only explicit entry/exit facts prove that.
using FlowLoopContext=std::variant<IncomingScope,UnmodeledScope,StmtId,ExitedLoopScope>;
struct FlowLoopScopeEvent {FlowLoopContext before=UnmodeledScope{},after=UnmodeledScope{};};
struct FlowLoopScope {std::optional<StmtId> parent;};
struct ScopePosition {std::size_t value=0;auto operator<=>(const ScopePosition&)const=default;};
struct FlowScopeRegion {ScopePosition first,last;};
// Heavy paths partition the same forest. Positions are explicit index
// mappings, not ScopeId/StmtId casts; ancestor path differences take O(log S).
struct FlowScopePath {std::optional<ScopePosition> parent;ScopePosition head;std::uint32_t depth=0;};
enum class FlowScopePhase : std::uint8_t {kBefore,kAfter};
enum class FlowUnitPresence : std::uint8_t {kActive,kInactive,kUnmodeled,kCompletionOnly};
enum class FlowUnitTransfer : std::uint8_t {kPreserved,kEnded,kUnmodeled};
// Immutable per-projection lexical activation contexts. Euler intervals index
// containment, never runtime cell equality. Unknown cleanup must retain value
// demand while invalidating substitutions, rather than pretend a definite exit.
struct FlowScopes {
  std::vector<FlowScopeRegion> regions;
  std::vector<FlowScopePath> paths;
  std::vector<FlowScopeEvent> events;
  std::vector<FlowScopeRegion> loop_regions;
  std::vector<FlowScopePath> loop_paths;
  std::vector<FlowLoopScopeEvent> loop_events;
  [[nodiscard]] FlowUnitPresence Presence(ScopeId,FlowPoint,FlowScopePhase) const;
  [[nodiscard]] FlowUnitTransfer Transfer(ScopeId,FlowPoint,FlowPoint) const;
  [[nodiscard]] FlowUnitPresence LoopPresence(StmtId,FlowPoint,FlowScopePhase) const;
  [[nodiscard]] FlowUnitTransfer LoopTransfer(StmtId,FlowPoint,FlowPoint) const;
};
[[nodiscard]] std::shared_ptr<FlowScopes> MakeFlowScopes(const FunctionTree&,TreeCheckBudget&);
void IndexFlowLoopScopes(FlowScopes&,std::span<const std::optional<FlowLoopScope>>,TreeCheckBudget&);
}
#endif
