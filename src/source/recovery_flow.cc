#include "recovery_flow.h"
#include "binding_initializer.h"
#include "recovery_scratch.h"
#include "expression_children.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error=TreeStructureError;
using Point=FlowPoint;
struct Context {Point next,thrown;std::optional<Point> finalizer;};
struct Statement {StmtId id;Context context;};
struct Expression {ExprId id;Context context;};
using Task=std::variant<Statement,Expression>;
enum class ReadState : std::uint8_t {kUnseen,kPending,kDone};
class FlowBuilder {
 public:
  FlowBuilder(const FunctionTree& tree,LegacyFlowProjection projection,analysis::SemanticBudget budget)
      : tree_(tree), projection_(projection), budget_(budget), memory_(budget_), events_(&memory_), edges_(&memory_),
        statements_(&memory_), expressions_(&memory_), exits_(&memory_), tasks_(&memory_), seen_(&memory_), definitions_(&memory_), reads_(&memory_), read_states_(&memory_), initializers_(&memory_) {}
  RecoveryFlow Run() {
    ProjectedReads();
    initializers_.resize(tree_.expressions.size());
    for (std::size_t i=0;i<projection_.initializers.size();++i) {
      budget_.Step();
      if (const auto& initializer=projection_.initializers[i])
        initializers_[initializer->assignment.value]=StmtId{static_cast<std::uint32_t>(i)};
    }
    terminal_=New(); dispatch_=New();
    statements_.resize(tree_.statements.size()); exits_.resize(tree_.statements.size());
    expressions_.resize(tree_.expressions.size()); seen_.resize(tree_.expressions.size());
    for(auto& point:statements_){budget_.Step();point=New();}
    for(auto& point:expressions_){budget_.Step();point=New();}
    Link(dispatch_,terminal_,FlowCompletion::kConservativeCleanup);
    entry_=New();auto previous=entry_;
    for(const auto& parameter:tree_.parameters) {
      budget_.Step();const auto initialized=Define(parameter.symbol,FlowDefinitionKind::kParameter);
      Link(previous,initialized);previous=initialized;
    }
    Link(previous,statements_[tree_.body.value]);
    tasks_.push_back(Statement{tree_.body,{terminal_,terminal_,{}}});
    while(!tasks_.empty()) {budget_.Step();const auto task=tasks_.back();tasks_.pop_back();std::visit([&](auto at){Build(at);},task);}
    RecoveryFlow result;
    result.identity=tree_.identity;result.value_revision=tree_.value_revision;result.cfg_revision=tree_.cfg_revision;
    result.entry=entry_;result.terminal=terminal_;
    const auto copy=[&](auto& destination,const auto& source){budget_.Array(destination,source.size());std::copy(source.begin(),source.end(),destination.begin());};
    copy(result.events,events_);copy(result.edges,edges_);copy(result.definitions,definitions_);
    copy(result.statements,statements_);copy(result.expressions,expressions_);
    budget_.Array(result.bindings,tree_.symbols.size());
    for(std::size_t i=0;i<tree_.symbols.size();++i){budget_.Step();result.bindings[i]=tree_.symbols[i].kind;}
    result.allocated_bytes=budget_.Bytes();result.work=budget_.Work();return result;
  }
 private:
  Point New(FlowEvent event={}) {
    RequireTree(events_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    budget_.Step();Point result{static_cast<std::uint32_t>(events_.size())};events_.push_back(event);return result;
  }
  Point Define(SymbolId binding,FlowDefinitionKind kind,std::optional<SymbolId> copy={},
      std::optional<ExprId> value={},std::optional<StmtId> declaration={},
      std::optional<ExprId> write={},FlowWriteEffect effect=FlowWriteEffect::kObservable) {
    RequireTree(definitions_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    const auto point=New({FlowEventKind::kDefinition,{},FlowDefinitionId{static_cast<std::uint32_t>(definitions_.size())}});
    definitions_.push_back({point,binding,kind,copy,value,declaration,write,effect});return point;
  }
  void Link(Point from,Point to,FlowCompletion completion=FlowCompletion::kNormal) {
    budget_.Step();edges_.push_back({from,to,completion,events_[to.value].incoming,events_[from.value].outgoing});
    events_[to.value].incoming=events_[from.value].outgoing=edges_.size()-1;
  }
  SymbolId Resolve(SymbolId symbol) const {return projection_.aliases[symbol.value].value_or(symbol);}
  void ProjectedReads() {
    reads_.resize(tree_.expressions.size());read_states_.resize(tree_.expressions.size());
    std::pmr::vector<ExprId> path(&memory_);
    // Resolve replacement chains once, including forwarded copy RHSs. Arena
    // order is immaterial and each edge is visited once, not per affinity.
    for (std::size_t i=0;i<tree_.expressions.size();++i) {
      budget_.Step();ExprId id{static_cast<std::uint32_t>(i)};path.clear();
      while (read_states_[id.value]==ReadState::kUnseen) {
        budget_.Step();read_states_[id.value]=ReadState::kPending;path.push_back(id);
        if (const auto replacement=projection_.replacements[id.value]) {id=*replacement;continue;}
        if (const auto* read=std::get_if<SymbolExpr>(&tree_.expressions[id.value].expression);
            read&&!projection_.literals[read->symbol.value])reads_[id.value]=Resolve(read->symbol);
        read_states_[id.value]=ReadState::kDone;break;
      }
      RequireTree(read_states_[id.value]==ReadState::kDone,Error::kOwnership);
      for (const auto at:path) {budget_.Step();reads_[at.value]=reads_[id.value];read_states_[at.value]=ReadState::kDone;}
    }
  }
  std::optional<SymbolId> Read(ExprId id) const {return reads_[id.value];}
  Point Expr(ExprId id,Context context){tasks_.push_back(Expression{id,context});return expressions_[id.value];}
  Point Stmt(StmtId id,Context context){tasks_.push_back(Statement{id,context});return statements_[id.value];}
  void Abrupt(Point point,Point target,const Context& context,FlowCompletion completion) {
    Link(point,target,completion); // Bypass is a may-path, never evidence that cleanup is absent.
    if(context.finalizer){Link(point,*context.finalizer,FlowCompletion::kConservativeCleanup);Link(dispatch_,target,FlowCompletion::kConservativeCleanup);}
  }
  void Build(Expression at) {
    RequireTree(!seen_[at.id.value],Error::kOwnership);seen_[at.id.value]=1;
    const auto start=expressions_[at.id.value];const auto& context=at.context;
    if(auto replacement=projection_.replacements[at.id.value]){Link(start,Expr(*replacement,context));return;}
    const auto& node=tree_.expressions[at.id.value].expression;
    if(const auto* choice=std::get_if<ConditionalExpr>(&node)) {
      const auto branch=New({FlowEventKind::kTruthBranch,{},{},choice->condition});Link(start,Expr(choice->condition,{branch,context.thrown,context.finalizer}));
      Link(branch,Expr(choice->consequent,context),FlowCompletion::kTaken);Link(branch,Expr(choice->alternate,context),FlowCompletion::kNotTaken);return;
    }
    if(const auto* logical=std::get_if<LogicalExpr>(&node)) {
      const auto branch=New({logical->operation==TreeLogical::kNullish?FlowEventKind::kNullishBranch:FlowEventKind::kTruthBranch,{},{},logical->left});
      Link(start,Expr(logical->left,{branch,context.thrown,context.finalizer}));
      const auto right=logical->operation==TreeLogical::kOr?FlowCompletion::kNotTaken:FlowCompletion::kTaken;
      Link(branch,context.next,right==FlowCompletion::kTaken?FlowCompletion::kNotTaken:FlowCompletion::kTaken);
      Link(branch,Expr(logical->right,context),right);return;
    }
    const auto effect=New({FlowEventKind::kEvaluation,{},{},at.id});auto normal=context.next;
    if(const auto* read=std::get_if<SymbolExpr>(&node)) {
      if(!projection_.literals[read->symbol.value]){events_[effect.value].read=Resolve(read->symbol);events_[effect.value].kind=FlowEventKind::kRead;}
    }
    if(const auto* assignment=std::get_if<AssignmentExpr>(&node)) {
      if(const auto* target=std::get_if<SymbolTarget>(&assignment->target)) {
        const auto copied=Read(assignment->value);
        // A native source declaration initializes a binding at this commit;
        // dead-value elimination must not erase the lexical initialization.
        const auto declaration=initializers_[at.id.value];
        const auto kind=declaration?FlowDefinitionKind::kInitializer:FlowDefinitionKind::kAssignment;
        const bool inert=!projection_.private_bindings.empty() &&
            projection_.private_bindings[target->symbol.value] && projection_.normal_stores[at.id.value];
        normal=Define(target->symbol,kind,copied,assignment->value,declaration,at.id,
            inert?FlowWriteEffect::kInert:FlowWriteEffect::kObservable);Link(normal,context.next);
      }
    }
    if(const auto* update=std::get_if<UpdateExpr>(&node)) {
      normal=Define(update->symbol,FlowDefinitionKind::kUpdate,{},at.id);Link(normal,context.next);
    }
    const bool suspension=std::holds_alternative<AwaitExpr>(node)||std::holds_alternative<YieldExpr>(node);
    Link(effect,normal,suspension?FlowCompletion::kResumeNext:FlowCompletion::kNormal);
    if(IntrinsicEvaluation(node)==ExpressionEvaluation::kRequired &&
        projection_.evaluations[at.id.value]!=ExpressionEvaluation::kInert &&
        !projection_.normal_stores[at.id.value])Link(effect,context.thrown,suspension?FlowCompletion::kResumeThrow:FlowCompletion::kThrow);
    if(suspension)events_[effect.value].kind=FlowEventKind::kSuspend;
    if(std::holds_alternative<YieldExpr>(node))Abrupt(effect,terminal_,context,FlowCompletion::kResumeReturn);
    // Children keep language evaluation order, including callee, receiver and
    // assignment RHS. Their commits are distinct from throwing evaluation.
    std::pmr::vector<ExprId> children(&memory_);
    VisitExpressionChildren(tree_,node,[&](ExprId child){budget_.Step();children.push_back(child);});
    auto next=effect;
    if(const auto* update=std::get_if<UpdateExpr>(&node)) {
      const auto read=New({FlowEventKind::kRead,update->symbol,{},at.id});Link(read,effect);
      Link(read,context.thrown,FlowCompletion::kThrow);next=read;
    }
    for(auto i=children.rbegin();i!=children.rend();++i)next=Expr(*i,{next,context.thrown,context.finalizer});
    Link(start,next);
  }
  void Build(Statement at) {
    const auto start=statements_[at.id.value];const auto context=at.context;
    exits_[at.id.value]=context.next;
    if(projection_.omitted[at.id.value]){Link(start,context.next);return;}
    std::visit([&](const auto& node){
      using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,BlockStmt>) {
        const auto entered=New({FlowEventKind::kEnterScope,{},{},{},node.scope});
        auto next=New({FlowEventKind::kLeaveScope,{},{},{},node.scope});Link(next,context.next);
        for(std::size_t i=node.child_count;i;--i)next=Stmt(tree_.children[node.first_child+i-1],{next,context.thrown,context.finalizer});
        Link(entered,next);Link(start,entered);
      } else if constexpr(std::is_same_v<T,ExpressionStmt>)Link(start,Expr(node.expression,context));
      else if constexpr(std::is_same_v<T,DeclarationStmt>) {
        auto commit=Define(node.symbol,node.initializer?FlowDefinitionKind::kInitializer:FlowDefinitionKind::kUndefined,{},node.initializer,at.id);Link(commit,context.next);
        Link(start,node.initializer?Expr(*node.initializer,{commit,context.thrown,context.finalizer}):commit);
      } else if constexpr(std::is_same_v<T,IfStmt>) {
        auto branch=New({FlowEventKind::kTruthBranch,{},{},node.condition});Link(start,Expr(node.condition,{branch,context.thrown,context.finalizer}));
        Link(branch,Stmt(node.consequent,context),FlowCompletion::kTaken);Link(branch,node.alternate?Stmt(*node.alternate,context):context.next,FlowCompletion::kNotTaken);
      } else if constexpr(std::is_same_v<T,LoopStmt>)Link(start,Stmt(node.body,{start,context.thrown,context.finalizer}));
      else if constexpr(std::is_same_v<T,BreakStmt>)Abrupt(start,exits_[node.target.value],context,FlowCompletion::kJump);
      else if constexpr(std::is_same_v<T,ContinueStmt>)Abrupt(start,statements_[node.target.value],context,FlowCompletion::kJump);
      else if constexpr(std::is_same_v<T,ReturnStmt>) {
        auto returned=New();Abrupt(returned,terminal_,context,FlowCompletion::kReturn);
        Link(start,node.value?Expr(*node.value,{returned,context.thrown,context.finalizer}):returned);
      } else if constexpr(std::is_same_v<T,ThrowStmt>) {
        const auto thrown=New();Link(thrown,context.thrown,FlowCompletion::kThrow);
        Link(start,Expr(node.value,{thrown,context.thrown,context.finalizer}));
      }
      else if constexpr(std::is_same_v<T,TryStmt>) {
        auto normal=context.next,thrown=context.thrown;auto cleanup=context.finalizer;
        if(node.finalizer) {
          const auto resume=New();Link(resume,context.next);Link(resume,dispatch_,FlowCompletion::kConservativeCleanup);
          cleanup=Stmt(*node.finalizer,{resume,context.thrown,context.finalizer});normal=*cleanup;thrown=*cleanup;
          Link(dispatch_,*cleanup,FlowCompletion::kConservativeCleanup);Link(dispatch_,context.thrown,FlowCompletion::kConservativeCleanup);
        }
        if(node.handler) {
          const auto handler=Stmt(node.handler->body,{normal,thrown,cleanup});
          const auto binding=Define(node.handler->parameter,FlowDefinitionKind::kCatch);Link(binding,handler);thrown=binding;
        }
        Link(start,Stmt(node.body,{normal,thrown,cleanup}));
      } else if constexpr(std::is_same_v<T,DebuggerStmt>)Link(start,context.next);
      else {static_assert(std::is_same_v<T,UnrecoveredStmt>);throw Error::kUnsupportedSyntax;}
    },tree_.statements[at.id.value].statement);
  }
  const FunctionTree& tree_;LegacyFlowProjection projection_;TreeCheckBudget budget_;RecoveryScratch memory_;
  std::pmr::vector<FlowEvent> events_;std::pmr::vector<FlowEdge> edges_;
  std::pmr::vector<Point> statements_,expressions_,exits_;std::pmr::vector<Task> tasks_;
  std::pmr::vector<std::uint8_t> seen_;std::pmr::vector<FlowDefinition> definitions_;
  std::pmr::vector<std::optional<SymbolId>> reads_;std::pmr::vector<ReadState> read_states_;
  std::pmr::vector<std::optional<StmtId>> initializers_;
  Point entry_{},terminal_{},dispatch_{};
};
}
std::variant<RecoveryFlow,TreeStructureError> BuildRecoveryFlow(
    const FunctionTree& tree,LegacyFlowProjection projection,analysis::SemanticBudget budget) {
  try{return FlowBuilder(tree,projection,budget).Run();}
  catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
