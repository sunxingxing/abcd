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
struct Context {
  Point next,thrown;
  std::optional<Point> finalizer;
  std::optional<DiscardedId> discarded;
  FlowValueUse value_use=FlowValueUse::kObserved;
  std::optional<StmtId> owner;
  Context Operand(Point continuation,FlowValueUse use=FlowValueUse::kObserved) const {
    auto result=*this;result.next=continuation;result.value_use=use;return result;
  }
};
struct Statement {StmtId id;Context context;};
struct Expression {ExprId id;Context context;};
using Task=std::variant<Statement,Expression>;
enum class ReadState : std::uint8_t {kUnseen,kPending,kDone};
class FlowBuilder {
 public:
  FlowBuilder(const FunctionTree& tree,LegacyFlowProjection projection,analysis::SemanticBudget budget,RecoveryFlow storage)
      : tree_(tree), projection_(projection), budget_(budget), memory_(budget_),
        statements_(&memory_), expressions_(&memory_), exits_(&memory_), tasks_(&memory_), seen_(&memory_), reads_(&memory_), read_states_(&memory_), initializers_(&memory_), result_(std::move(storage)) {
    events_=std::move(result_.events);events_.clear();edges_=std::move(result_.edges);edges_.clear();
    definitions_=std::move(result_.definitions);definitions_.clear();references_=std::move(result_.references);references_.clear();
    discarded_=std::move(result_.discarded);discarded_.clear();
  }
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
    Resize(result_.value_uses,tree_.expressions.size());
    for(auto& use:result_.value_uses){budget_.Step();use=FlowValueUse::kUnmodeled;}
    for(auto& point:statements_){budget_.Step();point=New();}
    for(auto& point:expressions_){budget_.Step();point=New();}
    Link(dispatch_,terminal_,FlowCompletion::kConservativeCleanup);
    entry_=New();auto previous=entry_;
    for(std::size_t i=0;i<tree_.symbols.size();++i) {
      budget_.Step();if(tree_.symbols[i].kind!=BindingKind::kFunctionSelf)continue;
      const auto initialized=Define(SymbolId{static_cast<std::uint32_t>(i)},FlowDefinitionKind::kFunctionSelf);
      Link(previous,initialized);previous=initialized;
    }
    for(const auto& parameter:tree_.parameters) {
      budget_.Step();const auto initialized=Define(parameter.symbol,FlowDefinitionKind::kParameter);
      Link(previous,initialized);previous=initialized;
    }
    Link(previous,statements_[tree_.body.value]);
    tasks_.push_back(Statement{tree_.body,{terminal_,terminal_,{}}});
    while(!tasks_.empty()) {budget_.Step();const auto task=tasks_.back();tasks_.pop_back();std::visit([&](auto at){Build(at);},task);}
    auto& result=result_;
    result.identity=tree_.identity;result.value_revision=tree_.value_revision;result.cfg_revision=tree_.cfg_revision;
    result.entry=entry_;result.terminal=terminal_;
    const auto copy=[&](auto& destination,const auto& source){Resize(destination,source.size());std::copy(source.begin(),source.end(),destination.begin());};
    result.events=std::move(events_);result.edges=std::move(edges_);
    result.definitions=std::move(definitions_);result.references=std::move(references_);
    result.discarded=std::move(discarded_);
    copy(result.statements,statements_);copy(result.expressions,expressions_);
    Resize(result.bindings,tree_.symbols.size());
    for(std::size_t i=0;i<tree_.symbols.size();++i) {
      budget_.Step();result.bindings[i]={tree_.symbols[i].kind,
          !projection_.private_bindings.empty() && projection_.private_bindings[i]
              ? FlowBindingExposure::kPrivate : FlowBindingExposure::kObservable};
    }
    // Native source declarations supersede their original empty placeholders.
    // This is the supplied declaration plan, not inferred first-use syntax.
    for(const auto& definition:result.definitions) {
      budget_.Step();auto& binding=result.bindings[definition.binding.value];
      if(definition.kind==FlowDefinitionKind::kInitializer ||
          (definition.kind==FlowDefinitionKind::kUndefined && !binding.declaration))
        binding.declaration=definition.declaration;
    }
    result.allocated_bytes=budget_.Bytes();result.work=budget_.Work();return std::move(result);
  }
 private:
  template<class T> void Resize(std::vector<T>& values,std::size_t count) {
    if(count>values.capacity()) {
      budget_.Admit(count,sizeof(T));values.reserve(count);
      budget_.Admit(values.capacity()-count,sizeof(T));
    }
    budget_.Step(count);values.resize(count);
  }
  template<class T> void Append(std::vector<T>& values,T value) {
    // The published graph owns these buffers directly. Charge geometric growth
    // including old/new overlap, then move ownership instead of allocating a
    // second copy at publication. Scratch still uses the scoped PMR resource.
    if(values.size()==values.capacity()) {
      constexpr std::size_t growth=2;
      RequireTree(values.size()<values.max_size(),Error::kResourceLimit);
      const auto capacity=values.capacity()?values.capacity()>values.max_size()/growth?
          values.max_size():values.capacity()*growth:1;
      budget_.Admit(capacity,sizeof(T));values.reserve(capacity);
      budget_.Admit(values.capacity()-capacity,sizeof(T));
    }
    budget_.Step();values.push_back(std::move(value));
  }
  Point New(FlowEvent event={}) {
    RequireTree(events_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    budget_.Step();Point result{static_cast<std::uint32_t>(events_.size())};Append(events_,event);return result;
  }
  Point Define(SymbolId binding,FlowDefinitionKind kind,std::optional<SymbolId> copy={},
      std::optional<ExprId> value={},std::optional<StmtId> declaration={},
      std::optional<ExprId> write={},FlowWriteEffect effect=FlowWriteEffect::kObservable,
      std::optional<DiscardedId> discarded={},std::optional<FlowExceptionContext> exceptions={},
      std::optional<StmtId> owner={}) {
    RequireTree(definitions_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    const auto point=New({FlowEventKind::kDefinition,{},FlowDefinitionId{static_cast<std::uint32_t>(definitions_.size())}});
    events_[point.value].discarded=discarded;
    Append(definitions_,{point,Resolve(binding),kind,copy,value,declaration,write,effect});
    if(write)Reference(point,Resolve(binding),FlowReferenceKind::kWrite,*write,exceptions,owner);
    else if(kind==FlowDefinitionKind::kUndefinedStore)Reference(point,Resolve(binding),FlowReferenceKind::kWrite,*declaration,exceptions,owner);
    return point;
  }
  void Reference(FlowPoint point,SymbolId binding,FlowReferenceKind kind,FlowReferenceSite site,std::optional<FlowExceptionContext> exceptions={},std::optional<StmtId> owner={}) {
    budget_.Step();Append(references_,{point,binding,kind,site,exceptions,owner});
  }
  void Link(Point from,Point to,FlowCompletion completion=FlowCompletion::kNormal) {
    budget_.Step();Append(edges_,{from,to,completion,events_[to.value].incoming,events_[from.value].outgoing});
    events_[to.value].incoming=events_[from.value].outgoing=edges_.size()-1;
  }
  SymbolId Resolve(SymbolId symbol) const {
    const auto value=projection_.aliases[symbol.value].value_or(symbol);
    return projection_.storage.empty()?value:projection_.storage[value.value].value_or(value);
  }
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
    result_.value_uses[at.id.value]=context.value_use;
    if(auto replacement=projection_.replacements[at.id.value]){Link(start,Expr(*replacement,context));return;}
    const auto& node=tree_.expressions[at.id.value].expression;
    if(const auto* choice=std::get_if<ConditionalExpr>(&node)) {
      const auto branch=New({FlowEventKind::kTruthBranch,{},{},choice->condition});Link(start,Expr(choice->condition,context.Operand(branch)));
      Link(branch,Expr(choice->consequent,context),FlowCompletion::kTaken);Link(branch,Expr(choice->alternate,context),FlowCompletion::kNotTaken);return;
    }
    if(const auto* logical=std::get_if<LogicalExpr>(&node)) {
      const auto branch=New({logical->operation==TreeLogical::kNullish?FlowEventKind::kNullishBranch:FlowEventKind::kTruthBranch,{},{},logical->left});
      Link(start,Expr(logical->left,context.Operand(branch)));
      const auto right=logical->operation==TreeLogical::kOr?FlowCompletion::kNotTaken:FlowCompletion::kTaken;
      Link(branch,context.next,right==FlowCompletion::kTaken?FlowCompletion::kNotTaken:FlowCompletion::kTaken);
      Link(branch,Expr(logical->right,context),right);return;
    }
    const auto effect=New({FlowEventKind::kEvaluation,{},{},at.id});auto normal=context.next;
    events_[effect.value].discarded=context.discarded;
    bool required=IntrinsicEvaluation(node)==ExpressionEvaluation::kRequired &&
        projection_.evaluations[at.id.value]!=ExpressionEvaluation::kInert;
    if(const auto* read=std::get_if<SymbolExpr>(&node)) {
      if(!projection_.literals[read->symbol.value]) {
        events_[effect.value].read=Resolve(read->symbol);events_[effect.value].kind=FlowEventKind::kRead;
        Reference(effect,Resolve(read->symbol),FlowReferenceKind::kRead,at.id,FlowExceptionContext{context.thrown,context.finalizer},context.owner);
      }
    }
    if(const auto* assignment=std::get_if<AssignmentExpr>(&node)) {
      if(const auto* target=std::get_if<SymbolTarget>(&assignment->target)) {
        const auto copied=Read(assignment->value);
        // A native source declaration initializes a binding at this commit;
        // dead-value elimination must not erase the lexical initialization.
        const auto declaration=initializers_[at.id.value];
        const auto kind=declaration?FlowDefinitionKind::kInitializer:FlowDefinitionKind::kAssignment;
        const bool inert=!projection_.private_bindings.empty() &&
            projection_.private_bindings[Resolve(target->symbol).value] && projection_.normal_stores[at.id.value];
        normal=Define(target->symbol,kind,copied,assignment->value,declaration,at.id,
            inert?FlowWriteEffect::kInert:FlowWriteEffect::kObservable,context.discarded,FlowExceptionContext{context.thrown,context.finalizer},context.owner);Link(normal,context.next);
        required=!inert || kind==FlowDefinitionKind::kInitializer;
      }
    }
    if(const auto* update=std::get_if<UpdateExpr>(&node)) {
      normal=Define(update->symbol,FlowDefinitionKind::kUpdate,{},at.id,{},at.id,FlowWriteEffect::kObservable,context.discarded,FlowExceptionContext{context.thrown,context.finalizer},context.owner);Link(normal,context.next);
    }
    // Region effects are composed from semantic operations, independently of
    // whether their result is later used. Inert private writes can be dropped;
    // initialization, getters, coercion, calls, throws and suspension remain.
    events_[effect.value].effect=required?ExpressionEvaluation::kRequired:ExpressionEvaluation::kInert;
    if(context.discarded && required)discarded_[context.discarded->value].effect=ExpressionEvaluation::kRequired;
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
      const auto read=New({FlowEventKind::kRead,Resolve(update->symbol),{},at.id});events_[read.value].discarded=context.discarded;Link(read,effect);
      events_[read.value].effect=ExpressionEvaluation::kRequired;
      Reference(read,Resolve(update->symbol),FlowReferenceKind::kRead,at.id,FlowExceptionContext{context.thrown,context.finalizer},context.owner);
      Link(read,context.thrown,FlowCompletion::kThrow);next=read;
    }
    for(auto i=children.rbegin();i!=children.rend();++i) {
      // Comma does not observe its left result; only its right transports the
      // parent's value use. All other operands here feed an actual operation.
      auto use=FlowValueUse::kObserved;
      if(const auto* sequence=std::get_if<SequenceExpr>(&node))
        use=*i==sequence->left?FlowValueUse::kDiscarded:context.value_use;
      next=Expr(*i,context.Operand(next,use));
    }
    Link(start,next);
  }
  void Build(Statement at) {
    const auto start=statements_[at.id.value];auto context=at.context;context.owner=at.id;
    exits_[at.id.value]=context.next;
    if(projection_.omitted[at.id.value]){Link(start,context.next);return;}
    std::visit([&](const auto& node){
      using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,BlockStmt>) {
        const auto entered=New({FlowEventKind::kEnterScope,{},{},{},node.scope});
        auto next=New({FlowEventKind::kLeaveScope,{},{},{},node.scope});Link(next,context.next);
        for(std::size_t i=node.child_count;i;--i)next=Stmt(tree_.children[node.first_child+i-1],{next,context.thrown,context.finalizer});
        Link(entered,next);Link(start,entered);
      } else if constexpr(std::is_same_v<T,ExpressionStmt>) {
        RequireTree(discarded_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
        const DiscardedId id{static_cast<std::uint32_t>(discarded_.size())};
        Append(discarded_,{at.id});
        auto evaluation=context;evaluation.discarded=id;evaluation.value_use=FlowValueUse::kDiscarded;Link(start,Expr(node.expression,evaluation));
      }
      else if constexpr(std::is_same_v<T,DeclarationStmt>) {
        if(!projection_.undefined_stores.empty())if(const auto target=projection_.undefined_stores[at.id.value]) {
          const auto commit=Define(*target,FlowDefinitionKind::kUndefinedStore,{},{},at.id,{},FlowWriteEffect::kInert,{},FlowExceptionContext{context.thrown,context.finalizer},context.owner);
          Link(start,commit);Link(commit,context.next);return;
        }
        auto commit=Define(node.symbol,node.initializer?FlowDefinitionKind::kInitializer:FlowDefinitionKind::kUndefined,{},node.initializer,at.id);Link(commit,context.next);
        Link(start,node.initializer?Expr(*node.initializer,context.Operand(commit)):commit);
      } else if constexpr(std::is_same_v<T,IfStmt>) {
        auto branch=New({FlowEventKind::kTruthBranch,{},{},node.condition});Link(start,Expr(node.condition,context.Operand(branch)));
        Link(branch,Stmt(node.consequent,context),FlowCompletion::kTaken);Link(branch,node.alternate?Stmt(*node.alternate,context):context.next,FlowCompletion::kNotTaken);
      } else if constexpr(std::is_same_v<T,LoopStmt>)Link(start,Stmt(node.body,{start,context.thrown,context.finalizer}));
      else if constexpr(std::is_same_v<T,BreakStmt>)Abrupt(start,exits_[node.target.value],context,FlowCompletion::kJump);
      else if constexpr(std::is_same_v<T,ContinueStmt>)Abrupt(start,statements_[node.target.value],context,FlowCompletion::kJump);
      else if constexpr(std::is_same_v<T,ReturnStmt>) {
        auto returned=New();Abrupt(returned,terminal_,context,FlowCompletion::kReturn);
        Link(start,node.value?Expr(*node.value,context.Operand(returned)):returned);
      } else if constexpr(std::is_same_v<T,ThrowStmt>) {
        const auto thrown=New();Link(thrown,context.thrown,FlowCompletion::kThrow);
        Link(start,Expr(node.value,context.Operand(thrown)));
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
      } else if constexpr(std::is_same_v<T,DebuggerStmt>) {
        events_[start.value].effect=ExpressionEvaluation::kRequired;Link(start,context.next);
      }
      else {static_assert(std::is_same_v<T,UnrecoveredStmt>);throw Error::kUnsupportedSyntax;}
    },tree_.statements[at.id.value].statement);
  }
  const FunctionTree& tree_;LegacyFlowProjection projection_;TreeCheckBudget budget_;RecoveryScratch memory_;
  std::vector<FlowEvent> events_;std::vector<FlowEdge> edges_;
  std::pmr::vector<Point> statements_,expressions_,exits_;std::pmr::vector<Task> tasks_;
  std::pmr::vector<std::uint8_t> seen_;std::vector<FlowDefinition> definitions_;
  std::pmr::vector<std::optional<SymbolId>> reads_;std::pmr::vector<ReadState> read_states_;
  std::pmr::vector<std::optional<StmtId>> initializers_;
  std::vector<DiscardedEvaluation> discarded_;
  std::vector<FlowReference> references_;
  Point entry_{},terminal_{},dispatch_{};
  RecoveryFlow result_;
};
}
std::variant<RecoveryFlow,TreeStructureError> BuildRecoveryFlow(
    const FunctionTree& tree,LegacyFlowProjection projection,analysis::SemanticBudget budget,RecoveryFlow storage) {
  try{return FlowBuilder(tree,projection,budget,std::move(storage)).Run();}
  catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
std::variant<FlowRefinement,TreeStructureError> RefineRecoveryFlowEffects(
    RecoveryFlow& flow,const FunctionTree& tree,std::span<const ExpressionEvaluation> evaluations,
    std::span<const std::uint8_t> normal_stores,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);
    std::vector<ExpressionEvaluation> assignments;
    budget.Array(assignments,tree.expressions.size());
    for(auto& effect:assignments){budget.Step();effect=ExpressionEvaluation::kRequired;}
    for(auto& definition:flow.definitions) {
      budget.Step();if(!definition.write || (definition.kind!=FlowDefinitionKind::kAssignment &&
          definition.kind!=FlowDefinitionKind::kInitializer))continue;
      const bool inert=flow.bindings[definition.binding.value].exposure==FlowBindingExposure::kPrivate &&
          normal_stores[definition.write->value];
      definition.effect=inert?FlowWriteEffect::kInert:FlowWriteEffect::kObservable;
      assignments[definition.write->value]=inert && definition.kind!=FlowDefinitionKind::kInitializer?
          ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired;
    }
    for(auto& discarded:flow.discarded){budget.Step();discarded.effect=ExpressionEvaluation::kInert;}
    for(auto& event:flow.events) {
      budget.Step();event.incoming.reset();event.outgoing.reset();
      if(!event.expression || event.kind==FlowEventKind::kTruthBranch || event.kind==FlowEventKind::kNullishBranch)continue;
      const auto id=*event.expression;const auto& node=tree.expressions[id.value].expression;
      bool required=IntrinsicEvaluation(node)==ExpressionEvaluation::kRequired &&
          evaluations[id.value]!=ExpressionEvaluation::kInert;
      if(std::holds_alternative<AssignmentExpr>(node))required=assignments[id.value]==ExpressionEvaluation::kRequired;
      event.effect=required?ExpressionEvaluation::kRequired:ExpressionEvaluation::kInert;
      if(event.discarded && required)flow.discarded[event.discarded->value].effect=ExpressionEvaluation::kRequired;
    }
    std::size_t retained=0;
    for(std::size_t i=0;i<flow.edges.size();++i) {
      budget.Step();auto edge=flow.edges[i];const auto& event=flow.events[edge.from.value];
      if(edge.completion==FlowCompletion::kThrow && event.expression) {
        const auto id=*event.expression;const auto& node=tree.expressions[id.value].expression;
        // Update's binding read and coercion are separate events. A normal
        // write summary never licenses deleting a coercion's throwing path.
        const bool update_read=event.kind==FlowEventKind::kRead && std::holds_alternative<UpdateExpr>(node);
        if(!update_read && (evaluations[id.value]==ExpressionEvaluation::kInert || normal_stores[id.value]))continue;
      }
      edge.incoming=flow.events[edge.to.value].incoming;edge.outgoing=flow.events[edge.from.value].outgoing;
      flow.edges[retained]=edge;flow.events[edge.to.value].incoming=retained;
      flow.events[edge.from.value].outgoing=retained;++retained;
    }
    flow.edges.resize(retained);return FlowRefinement{budget.Bytes(),budget.Work()};
  }catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
