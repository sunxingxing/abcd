#ifndef ABCD_SOURCE_RECOVERY_CHANGES_H_
#define ABCD_SOURCE_RECOVERY_CHANGES_H_
#include "binding_initializer.h"
#include "tree_checks.h"
#include <limits>
#include <array>

namespace abcd::source {
// Actual writes, not pass names, own invalidation. Primitives are immutable;
// evaluations, normal_stores and printer prefix counts are derived outputs.
enum class RecoveryDomain : std::uint8_t {kValues,kExecution,kDefinitions,kStorage,kNames,kGrammar};
class RecoveryDomains {
 public:
  constexpr RecoveryDomains(std::initializer_list<RecoveryDomain> domains={}) {
    for(auto domain:domains)bits_|=std::uint32_t{1}<<static_cast<unsigned>(domain);
  }
  constexpr void Add(RecoveryDomains domains){bits_|=domains.bits_;}
  [[nodiscard]] constexpr bool Intersects(RecoveryDomains domains) const {return (bits_&domains.bits_)!=0;}
  [[nodiscard]] constexpr bool Empty() const {return bits_==0;}
  [[nodiscard]] constexpr bool Contains(RecoveryDomains domains) const {return (bits_&domains.bits_)==domains.bits_;}
 private:
  std::uint32_t bits_=0;
};
struct RecoveryEpochs {
  std::array<std::uint64_t,static_cast<std::size_t>(RecoveryDomain::kGrammar)+1> values{};
  void Advance(RecoveryDomains changed,RecoveryDomains already) {
    for(std::size_t i=0;i<values.size();++i) {
      const RecoveryDomains domain{static_cast<RecoveryDomain>(i)};
      if(!changed.Contains(domain)||already.Contains(domain))continue;
      RequireTree(values[i]<std::numeric_limits<std::uint64_t>::max(),TreeStructureError::kResourceLimit);
      ++values[i];
    }
  }
  [[nodiscard]] bool Same(const RecoveryEpochs& other,RecoveryDomains inputs) const {
    for(std::size_t i=0;i<values.size();++i)
      if(inputs.Contains({static_cast<RecoveryDomain>(i)})&&values[i]!=other.values[i])return false;
    return true;
  }
};
enum class RecoveryField : std::uint8_t {
  kDeclarationSites,kDeclarationKinds,kDeclarations,kOmissions,kStatementValues,kForwardedValues,kLoopDeclarations,
  kLiterals,kReadLiterals,kReadBindings,kReadSources,kAliases,kStorage,
  kUndefinedStores,kDeclarationValues,kDeclarationCompletions,kReplacements,kTransports
};
struct RecoveryDeclarationSite {StmtId statement,container;bool operator==(const RecoveryDeclarationSite&) const = default;};
template<RecoveryField> struct RecoveryFieldTraits;
template<> struct RecoveryFieldTraits<RecoveryField::kDeclarationSites> {
  using Site=SymbolId;
  static constexpr RecoveryDomains domains{RecoveryDomain::kDefinitions,RecoveryDomain::kNames};
};
// Typed schema shared by the writer and its consumers. No semantic string
// dispatch or table-size snapshots: publication visits only actual writes.
#define ABCD_RECOVERY_FIELD(field,member,site,...) \
  template<> struct RecoveryFieldTraits<RecoveryField::field> { \
    using Site=site; \
    static auto& Values(BindingInitializers& plan){return plan.member;} \
    static constexpr RecoveryDomains domains{__VA_ARGS__}; \
  };
ABCD_RECOVERY_FIELD(kDeclarationKinds,statements,StmtId,RecoveryDomain::kGrammar)
ABCD_RECOVERY_FIELD(kDeclarations,statements,StmtId,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kOmissions,omitted,StmtId,RecoveryDomain::kExecution,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kStatementValues,statement_values,StmtId,RecoveryDomain::kExecution,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kForwardedValues,forwarded_values,StmtId,RecoveryDomain::kValues,RecoveryDomain::kExecution)
ABCD_RECOVERY_FIELD(kLoopDeclarations,loop_declarations,StmtId,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kLiterals,literals,SymbolId,RecoveryDomain::kValues)
ABCD_RECOVERY_FIELD(kReadLiterals,read_literals,ExprId,RecoveryDomain::kValues)
ABCD_RECOVERY_FIELD(kReadBindings,read_bindings,SymbolId,RecoveryDomain::kValues,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kReadSources,read_sources,ExprId,RecoveryDomain::kValues,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kAliases,aliases,SymbolId,RecoveryDomain::kValues,RecoveryDomain::kStorage,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kStorage,storage,SymbolId,RecoveryDomain::kStorage,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kUndefinedStores,undefined_stores,StmtId,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kDeclarationValues,declaration_values,StmtId,RecoveryDomain::kValues,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kDeclarationCompletions,declaration_completions,StmtId,RecoveryDomain::kDefinitions,RecoveryDomain::kNames)
ABCD_RECOVERY_FIELD(kReplacements,replacements,ExprId,RecoveryDomain::kValues,RecoveryDomain::kExecution,RecoveryDomain::kDefinitions)
ABCD_RECOVERY_FIELD(kTransports,transports,ExprId,RecoveryDomain::kValues,RecoveryDomain::kExecution,RecoveryDomain::kDefinitions)
#undef ABCD_RECOVERY_FIELD
using RecoverySite=std::variant<ExprId,StmtId,SymbolId>;
using RecoveryValue=std::variant<std::monostate,std::uint8_t,std::uint32_t,ExprId,SymbolId,
    PrimitiveId,BindingInitializer,DeclarationValue,DeclarationCompletion,ExpressionTransport,RecoveryDeclarationSite>;
struct RecoveryChange {RecoveryField field;RecoverySite site;RecoveryValue before,after;};
class RecoveryChanges {
 public:
  void Clear(){changes_.clear();domains_={};}
  [[nodiscard]] std::size_t AllocatedBytes() const {return changes_.capacity()*sizeof(RecoveryChange);}
  [[nodiscard]] bool Empty() const {return changes_.empty();}
  [[nodiscard]] RecoveryDomains Domains() const {return domains_;}
  [[nodiscard]] std::span<const RecoveryChange> Entries() const {return changes_;}
  template<RecoveryField field,class Value,class Commit>
  bool Write(BindingInitializers& plan,typename RecoveryFieldTraits<field>::Site site,
      Value value,TreeCheckBudget& budget,Commit&& commit) {
    return WriteIndexed<field>(RecoveryFieldTraits<field>::Values(plan),site,std::move(value),budget,std::forward<Commit>(commit));
  }
  template<RecoveryField field,class Entry,class Value,class Commit>
  bool WriteIndexed(std::vector<Entry>& values,typename RecoveryFieldTraits<field>::Site site,
      Value value,TreeCheckBudget& budget,Commit&& commit) {
    RequireTree(site.value<values.size(),TreeStructureError::kOwnership);budget.Step();
    const Entry next=std::move(value);
    if(values[site.value]==next)return false;
    if constexpr(field==RecoveryField::kDeclarationKinds) {
      RequireTree(values[site.value]&&next&&values[site.value]->symbol==next->symbol&&
          values[site.value]->assignment==next->assignment,TreeStructureError::kOwnership);
    }
    Record({field,site,ValueOf(values[site.value]),ValueOf(next)},budget);
    // Journal allocation precedes invalidation. After the callback there is
    // no analysis read or throwing allocation. The whole private preparation
    // is discarded if any later edit fails; no partial result is published.
    domains_.Add(RecoveryFieldTraits<field>::domains);
    commit();values[site.value]=next;return true;
  }
 private:
  template<class T> static RecoveryValue ValueOf(const T& value){return value;}
  template<class T> static RecoveryValue ValueOf(const std::optional<T>& value){
    return value?RecoveryValue{*value}:RecoveryValue{std::monostate{}};
  }
  void Record(RecoveryChange change,TreeCheckBudget& budget) {
    if(changes_.size()==changes_.capacity()) {
      // Geometric growth gives amortized linear journaling. Admit both old
      // and new buffers during reserve, then release the old owned capacity.
      const auto old=changes_.capacity();
      RequireTree(old<=std::numeric_limits<std::size_t>::max()/2,TreeStructureError::kResourceLimit);
      const auto capacity=old?old*2:1;
      budget.Admit(capacity,sizeof(RecoveryChange));changes_.reserve(capacity);
      budget.Admit(changes_.capacity()-capacity,sizeof(RecoveryChange));
      budget.Release(old*sizeof(RecoveryChange));
    }
    changes_.push_back(std::move(change));
  }
  std::vector<RecoveryChange> changes_;
  RecoveryDomains domains_;
};
}
#endif
