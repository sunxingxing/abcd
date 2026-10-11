#ifndef ABCD_SOURCE_RECOVERY_PROGRESS_H_
#define ABCD_SOURCE_RECOVERY_PROGRESS_H_
#include "recovery_changes.h"
namespace abcd::source {
// A successful semantic transaction consumes original occurrence obligations
// or joins binding classes. Auxiliary declaration/target rewrites travel with
// that transaction; changing their spelling alone cannot sustain another round.
// This checks algorithm invariants, never selects recoverable input shapes.
class RecoveryProgress {
 public:
  RecoveryProgress(std::size_t expressions,std::size_t statements,std::size_t bindings,TreeCheckBudget& budget) {
    budget.Array(expressions_,expressions);budget.Array(statements_,statements);
    budget.Array(literals_,bindings);budget.Array(parents_,bindings);budget.Array(ranks_,bindings);
    for(std::size_t i=0;i<bindings;++i){budget.Step();parents_[i]=SymbolId{static_cast<std::uint32_t>(i)};}
  }
  [[nodiscard]] std::uint64_t Witnesses() const {return witnesses_;}
  void Accept(std::span<const RecoveryChange> changes,bool grammar,TreeCheckBudget& budget) {
    bool progress=false;
    for(const auto& change:changes) {
      budget.Step();
      const auto absent=[](const RecoveryValue& value){return std::holds_alternative<std::monostate>(value);};
      const auto terminal=[&] {
        RequireTree(absent(change.before)&&!absent(change.after),TreeStructureError::kOwnership);
      };
      switch(change.field) {
        case RecoveryField::kReplacements:
          terminal();Consume(expressions_,std::get<ExprId>(change.site).value,Expression::kReplacement);progress=true;break;
        case RecoveryField::kTransports:
          terminal();Consume(expressions_,std::get<ExprId>(change.site).value,Expression::kTransport);progress=true;break;
        case RecoveryField::kReadLiterals:
          terminal();Consume(expressions_,std::get<ExprId>(change.site).value,Expression::kLiteral);progress=true;break;
        case RecoveryField::kReadSources:
          terminal();Consume(expressions_,std::get<ExprId>(change.site).value,Expression::kRead);progress=true;break;
        case RecoveryField::kLiterals:
          terminal();Consume(literals_,std::get<SymbolId>(change.site).value,Literal::kValue);progress=true;break;
        case RecoveryField::kStatementValues:
          terminal();Consume(statements_,std::get<StmtId>(change.site).value,Statement::kResidual);progress=true;break;
        case RecoveryField::kForwardedValues:
          terminal();Consume(statements_,std::get<StmtId>(change.site).value,Statement::kForwarded);progress=true;break;
        case RecoveryField::kOmissions:
          RequireTree(std::get<std::uint8_t>(change.before)==0&&std::get<std::uint8_t>(change.after)==1,TreeStructureError::kOwnership);
          Consume(statements_,std::get<StmtId>(change.site).value,Statement::kOmission);progress=true;break;
        case RecoveryField::kDeclarations:
          if(absent(change.before)&&!absent(change.after)) {
            // The original assignment becomes a lexical initializer once.
            // Removing that initializer later does not recreate its assignment.
            Consume(expressions_,std::get<BindingInitializer>(change.after).assignment.value,Expression::kInitializer);progress=true;
          }
          break;
        case RecoveryField::kDeclarationCompletions:
          if(absent(change.before)&&!absent(change.after)) {
            Consume(statements_,std::get<StmtId>(change.site).value,Statement::kInitialValue);progress=true;
          }
          break;
        case RecoveryField::kDeclarationValues:
          if(const auto* value=std::get_if<DeclarationValue>(&change.after);value&&!value->target) {
            const auto* previous=std::get_if<DeclarationValue>(&change.before);
            if(!previous||previous->target) {
              Consume(statements_,std::get<StmtId>(change.site).value,Statement::kStore);progress=true;
            }
          }
          break;
        case RecoveryField::kAliases:case RecoveryField::kStorage:
          // Representative compression is not another merge. Rank is a DSU
          // cost bound, not a semantic preference based on numeric binding IDs.
          if(const auto* target=std::get_if<SymbolId>(&change.after))
            progress=Join(std::get<SymbolId>(change.site),*target,budget)||progress;
          break;
        case RecoveryField::kReadBindings:
          // Production read substitution is occurrence-owned. Slot-indexed
          // facts remain an input adapter, never a new scheduling edit.
          throw TreeStructureError::kOwnership;
        case RecoveryField::kLoopDeclarations:
          RequireTree(grammar,TreeStructureError::kOwnership);break;
        case RecoveryField::kDeclarationSites:case RecoveryField::kDeclarationKinds:
        case RecoveryField::kUndefinedStores:break;
      }
    }
    // Grammar is an explicitly sealed finite tail. All other nonempty batches
    // require at least one never-repeated witness; a budget is not termination.
    RequireTree(changes.empty()||grammar||progress,TreeStructureError::kOwnership);
  }
 private:
  enum class Expression {kReplacement,kTransport,kLiteral,kRead,kInitializer};
  enum class Statement {kResidual,kForwarded,kOmission,kInitialValue,kStore};
  enum class Literal {kValue};
  template<class Kind> void Consume(std::vector<std::uint8_t>& seen,std::size_t index,Kind kind) {
    const auto bit=std::uint8_t{1}<<static_cast<unsigned>(kind);
    RequireTree(index<seen.size()&&!(seen[index]&bit),TreeStructureError::kOwnership);
    seen[index]|=bit;
    ++witnesses_;
  }
  SymbolId Root(SymbolId symbol,TreeCheckBudget& budget) {
    RequireTree(symbol.value<parents_.size(),TreeStructureError::kOwnership);
    auto root=symbol;
    while(parents_[root.value]!=root){budget.Step();root=parents_[root.value];}
    while(parents_[symbol.value]!=root){budget.Step();const auto next=parents_[symbol.value];parents_[symbol.value]=root;symbol=next;}
    return root;
  }
  bool Join(SymbolId first,SymbolId second,TreeCheckBudget& budget) {
    auto a=Root(first,budget),b=Root(second,budget);if(a==b)return false;
    if(ranks_[a.value]<ranks_[b.value])std::swap(a,b);
    parents_[b.value]=a;if(ranks_[a.value]==ranks_[b.value])++ranks_[a.value];
    ++witnesses_;
    return true;
  }
  std::vector<std::uint8_t> expressions_,statements_,literals_,ranks_;
  std::vector<SymbolId> parents_;
  std::uint64_t witnesses_=0;
};
}
#endif
