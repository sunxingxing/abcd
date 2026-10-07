#include "binding_interference.h"
#include "expression_children.h"
#include "expression_observation.h"
#include "tree_checks.h"
#include <algorithm>
#include <limits>
#include <memory_resource>
#include <set>
#include <stdexcept>
namespace abcd::source {
namespace {
using Error = TreeStructureError;
struct Point { std::uint32_t value; auto operator<=>(const Point&) const = default; };
struct Event { std::optional<SymbolId> read, write, copy; std::optional<std::size_t> incoming; };
struct Edge { Point from; std::optional<std::size_t> next; };
struct Context { Point next, thrown; std::optional<Point> finalizer; };
struct Statement { StmtId id; Context context; };
struct Expression { ExprId id; Context context; };
using Task = std::variant<Statement, Expression>;
enum class ReadState : std::uint8_t { kUnseen, kPending, kDone };
struct Live { Point point; SymbolId symbol; auto operator<=>(const Live&) const = default; };
class Scratch final : public std::pmr::memory_resource {
 public:
  explicit Scratch(TreeCheckBudget& budget) : budget_(budget) {}
 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    budget_.Admit(bytes,1); return std::pmr::new_delete_resource()->allocate(bytes,alignment);
  }
  void do_deallocate(void* p,std::size_t bytes,std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(p,bytes,alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {return this==&other;}
  TreeCheckBudget& budget_;
};
class Interference {
 public:
  Interference(const FunctionTree& tree,const BindingInitializers& projection,analysis::SemanticBudget budget)
      : tree_(tree), projection_(projection), budget_(budget), memory_(budget_), events_(&memory_), edges_(&memory_),
        statements_(&memory_), expressions_(&memory_), exits_(&memory_), tasks_(&memory_), seen_(&memory_), proposals_(&memory_), reads_(&memory_), read_states_(&memory_) {}
  BindingInterference Run() {
    ProjectedReads();
    terminal_=New(); dispatch_=New();
    statements_.resize(tree_.statements.size()); exits_.resize(tree_.statements.size());
    expressions_.resize(tree_.expressions.size()); seen_.resize(tree_.expressions.size());
    for(auto& point:statements_){budget_.Step();point=New();}
    for(auto& point:expressions_){budget_.Step();point=New();}
    Link(dispatch_,terminal_);
    tasks_.push_back(Statement{tree_.body,{terminal_,terminal_,{}}});
    while(!tasks_.empty()) {budget_.Step();const auto task=tasks_.back();tasks_.pop_back();std::visit([&](auto at){Build(at);},task);}
    // Ordered canonical pairs let interference queries avoid a dense all-pairs
    // graph. One sparse live fact is propagated once along each predecessor.
    const auto less=[&](const BindingAffinity& a,const BindingAffinity& b){budget_.Step();return Key(a)<Key(b);};
    std::sort(proposals_.begin(),proposals_.end(),less);
    proposals_.erase(std::unique(proposals_.begin(),proposals_.end(),[&](auto a,auto b){budget_.Step();return Key(a)==Key(b);}),proposals_.end());
    if(!proposals_.empty()) Liveness();
    BindingInterference result;budget_.Array(result.affinities,proposals_.size());
    std::copy(proposals_.begin(),proposals_.end(),result.affinities.begin());
    result.allocated_bytes=budget_.Bytes();result.work=budget_.Work();return result;
  }
 private:
  static std::pair<SymbolId,SymbolId> Key(const BindingAffinity& pair){return std::pair{pair.first,pair.second};}
  static BindingAffinity Pair(SymbolId a,SymbolId b){return a<b?BindingAffinity{a,b}:BindingAffinity{b,a};}
  Point New(Event event={}) {
    RequireTree(events_.size()<std::numeric_limits<std::uint32_t>::max(),Error::kResourceLimit);
    budget_.Step();Point result{static_cast<std::uint32_t>(events_.size())};events_.push_back(event);return result;
  }
  void Link(Point from,Point to) {budget_.Step();edges_.push_back({from,events_[to.value].incoming});events_[to.value].incoming=edges_.size()-1;}
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
  void Abrupt(Point point,Point target,const Context& context) {
    Link(point,target); // Bypass is a may-path, never evidence that cleanup is absent.
    if(context.finalizer){Link(point,*context.finalizer);Link(dispatch_,target);}
  }
  void Build(Expression at) {
    RequireTree(!seen_[at.id.value],Error::kOwnership);seen_[at.id.value]=1;
    const auto start=expressions_[at.id.value];const auto& context=at.context;
    if(auto replacement=projection_.replacements[at.id.value]){Link(start,Expr(*replacement,context));return;}
    const auto& node=tree_.expressions[at.id.value].expression;
    if(const auto* choice=std::get_if<ConditionalExpr>(&node)) {
      const auto branch=New();Link(start,Expr(choice->condition,{branch,context.thrown,context.finalizer}));
      Link(branch,Expr(choice->consequent,context));Link(branch,Expr(choice->alternate,context));return;
    }
    if(const auto* logical=std::get_if<LogicalExpr>(&node)) {
      const auto branch=New();Link(start,Expr(logical->left,{branch,context.thrown,context.finalizer}));
      Link(branch,context.next);Link(branch,Expr(logical->right,context));return;
    }
    const auto effect=New();auto normal=context.next;
    if(const auto* read=std::get_if<SymbolExpr>(&node)) {
      if(!projection_.literals[read->symbol.value])events_[effect.value].read=Resolve(read->symbol);
    }
    if(const auto* assignment=std::get_if<AssignmentExpr>(&node)) {
      if(const auto* target=std::get_if<SymbolTarget>(&assignment->target)) {
        const auto copied=Read(assignment->value);
        normal=New({{},target->symbol,copied,{}});Link(normal,context.next);
        if(copied&&*copied!=target->symbol&&
            tree_.symbols[copied->value].kind==BindingKind::kCompilerTemporary&&
            tree_.symbols[target->symbol.value].kind==BindingKind::kCompilerTemporary)
          proposals_.push_back(Pair(*copied,target->symbol));
      }
    }
    if(const auto* update=std::get_if<UpdateExpr>(&node)) {
      events_[effect.value].read=update->symbol;
      normal=New({{},update->symbol,{},{}});Link(normal,context.next);
    }
    Link(effect,normal);
    if(IntrinsicEvaluation(node)==ExpressionEvaluation::kRequired &&
        projection_.evaluations[at.id.value]!=ExpressionEvaluation::kInert &&
        !projection_.normal_stores[at.id.value])Link(effect,context.thrown);
    if(std::holds_alternative<YieldExpr>(node))Abrupt(effect,terminal_,context);
    // Children keep language evaluation order, including callee, receiver and
    // assignment RHS. Their commits are distinct from throwing evaluation.
    std::pmr::vector<ExprId> children(&memory_);
    VisitExpressionChildren(tree_,node,[&](ExprId child){budget_.Step();children.push_back(child);});
    auto next=effect;
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
        auto next=context.next;
        for(std::size_t i=node.child_count;i;--i)next=Stmt(tree_.children[node.first_child+i-1],{next,context.thrown,context.finalizer});
        Link(start,next);
      } else if constexpr(std::is_same_v<T,ExpressionStmt>)Link(start,Expr(node.expression,context));
      else if constexpr(std::is_same_v<T,DeclarationStmt>) {
        auto commit=New({{},node.symbol,{},{}});Link(commit,context.next);
        Link(start,node.initializer?Expr(*node.initializer,{commit,context.thrown,context.finalizer}):commit);
      } else if constexpr(std::is_same_v<T,IfStmt>) {
        auto branch=New();Link(start,Expr(node.condition,{branch,context.thrown,context.finalizer}));
        Link(branch,Stmt(node.consequent,context));Link(branch,node.alternate?Stmt(*node.alternate,context):context.next);
      } else if constexpr(std::is_same_v<T,LoopStmt>)Link(start,Stmt(node.body,{start,context.thrown,context.finalizer}));
      else if constexpr(std::is_same_v<T,BreakStmt>)Abrupt(start,exits_[node.target.value],context);
      else if constexpr(std::is_same_v<T,ContinueStmt>)Abrupt(start,statements_[node.target.value],context);
      else if constexpr(std::is_same_v<T,ReturnStmt>) {
        auto returned=New();Abrupt(returned,terminal_,context);
        Link(start,node.value?Expr(*node.value,{returned,context.thrown,context.finalizer}):returned);
      } else if constexpr(std::is_same_v<T,ThrowStmt>)Link(start,Expr(node.value,{context.thrown,context.thrown,context.finalizer}));
      else if constexpr(std::is_same_v<T,TryStmt>) {
        auto normal=context.next,thrown=context.thrown;auto cleanup=context.finalizer;
        if(node.finalizer) {
          const auto resume=New();Link(resume,context.next);Link(resume,dispatch_);
          cleanup=Stmt(*node.finalizer,{resume,context.thrown,context.finalizer});normal=*cleanup;thrown=*cleanup;
          Link(dispatch_,*cleanup);Link(dispatch_,context.thrown);
        }
        if(node.handler) {
          const auto handler=Stmt(node.handler->body,{normal,thrown,cleanup});
          const auto binding=New({{},node.handler->parameter,{},{}});Link(binding,handler);thrown=binding;
        }
        Link(start,Stmt(node.body,{normal,thrown,cleanup}));
      } else if constexpr(std::is_same_v<T,DebuggerStmt>)Link(start,context.next);
      else {static_assert(std::is_same_v<T,UnrecoveredStmt>);throw Error::kUnsupportedSyntax;}
    },tree_.statements[at.id.value].statement);
  }
  void Liveness() {
    std::pmr::vector<std::uint8_t> active(tree_.symbols.size(),0,&memory_);
    for(const auto& pair:proposals_){budget_.Step();active[pair.first.value]=active[pair.second.value]=1;}
    const auto less=[&](const Live& a,const Live& b){budget_.Step();return a<b;};
    std::pmr::set<Live,decltype(less)> facts(less,&memory_);std::pmr::vector<Live> pending(&memory_);
    const auto add=[&](Live fact){if(facts.insert(fact).second)pending.push_back(fact);};
    for(std::size_t i=0;i<events_.size();++i){budget_.Step();if(events_[i].read&&active[events_[i].read->value])add({{static_cast<std::uint32_t>(i)},*events_[i].read});}
    while(!pending.empty()) {
      const auto fact=pending.back();pending.pop_back();budget_.Step();
      for(auto edge=events_[fact.point.value].incoming;edge;edge=edges_[*edge].next) {
        budget_.Step();const auto predecessor=edges_[*edge].from;const auto& event=events_[predecessor.value];
        if(event.write&&*event.write!=fact.symbol&&event.copy!=fact.symbol) {
          const auto pair=Pair(*event.write,fact.symbol);
          const auto at=std::lower_bound(proposals_.begin(),proposals_.end(),pair,[&](auto a,auto b){budget_.Step();return Key(a)<Key(b);});
          if(at!=proposals_.end()&&Key(*at)==Key(pair))at->interference=true;
        }
        if(event.write!=fact.symbol)add({predecessor,fact.symbol});
      }
    }
  }
  const FunctionTree& tree_;const BindingInitializers& projection_;TreeCheckBudget budget_;Scratch memory_;
  std::pmr::vector<Event> events_;std::pmr::vector<Edge> edges_;
  std::pmr::vector<Point> statements_,expressions_,exits_;std::pmr::vector<Task> tasks_;
  std::pmr::vector<std::uint8_t> seen_;std::pmr::vector<BindingAffinity> proposals_;
  std::pmr::vector<std::optional<SymbolId>> reads_;std::pmr::vector<ReadState> read_states_;Point terminal_{},dispatch_{};
};
}
std::variant<BindingInterference,TreeStructureError> FindBindingInterference(
    const FunctionTree& tree,const BindingInitializers& projection,analysis::SemanticBudget budget) {
  try{return Interference(tree,projection,budget).Run();}
  catch(Error error){return error;}catch(const std::bad_alloc&){return Error::kResourceLimit;}
  catch(const std::length_error&){return Error::kResourceLimit;}
}
}
