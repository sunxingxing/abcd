#include "creator_classes.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = ClassTreeError;
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget) : initial_(budget), left_(budget) {}
  std::optional<analysis::OpId> operation;
  [[noreturn]] void Fail(Error error) const { throw CreatorClassFailure{error, operation}; }
  void Require(bool ok, Error error) const { if (!ok) Fail(error); }
  void Step(std::uint64_t n = 1) { Require(n <= left_.work, Error::kWorkLimit); left_.work -= n; }
  void Admit(std::size_t count, std::size_t width = 1) {
    Require(width && count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  template<class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Step(count); Admit(count, sizeof(T)); values.reserve(count); Admit(values.capacity() - count, sizeof(T));
  }
  template<class T> T Take(std::variant<T, ClassTreeFailure> result) {
    if (auto* error = std::get_if<ClassTreeFailure>(&result)) throw CreatorClassFailure{*error, operation};
    return std::get<T>(std::move(result));
  }
  analysis::SemanticBudget Left() const { return left_; }
  CreatorClassCost Cost() const { return {initial_.bytes-left_.bytes, initial_.work-left_.work}; }
 private:
  analysis::SemanticBudget initial_, left_;
};
// allocate_shared rebinds this allocator to its implementation control block.
// Charge that actual allocation size before allocating, rather than guessing
// a control-block multiplier. Destruction does not consult the expired quota.
template<class T> struct ClassAllocator {
  using value_type=T;
  Quota* quota;
  explicit ClassAllocator(Quota& q):quota(&q) {}
  template<class U> ClassAllocator(const ClassAllocator<U>& other):quota(other.quota) {}
  T* allocate(std::size_t count) {quota->Admit(count,sizeof(T));return std::allocator<T>{}.allocate(count);}
  void deallocate(T* p,std::size_t count) {std::allocator<T>{}.deallocate(p,count);}
  template<class U> bool operator==(const ClassAllocator<U>& other) const {return quota==other.quota;}
};
template<class F> auto Run(analysis::SemanticBudget budget, F&& body)
    -> std::variant<decltype(body(std::declval<Quota&>())), CreatorClassFailure> {
  Quota q(budget);
  try { return body(q); }
  catch (const CreatorClassFailure& error) { return error; }
  catch (const std::bad_alloc&) { return CreatorClassFailure{Error::kResourceLimit,q.operation}; }
  catch (const std::length_error&) { return CreatorClassFailure{Error::kResourceLimit,q.operation}; }
}
}
bool ValidClassOwnerRange(ClassOwnerRange range, std::uint64_t expected) {
  constexpr auto end = std::uint64_t{UINT32_MAX} + 1;
  return range.begin == expected && range.begin < range.end && range.end <= end;
}
std::variant<CreatorClasses, CreatorClassFailure> DiscoverCreatorClasses(
    const CreatorClassInput& input, analysis::SemanticBudget budget) {
  return Run(budget, [&](Quota& q) {
    CreatorClasses result;
    const auto& values = input.creator.values;
    result.occurrence=input.occurrence;result.identity=input.identity;
    result.creator=input.creator.input.Method();
    result.value_revision=values.graph.revision;result.cfg_revision=values.graph.cfg_revision;
    q.Reserve(result.definitions, values.definitions.sites.size());
    result.definitions.insert(result.definitions.end(), values.definitions.sites.begin(), values.definitions.sites.end());
    std::sort(result.definitions.begin(), result.definitions.end(), [&](const auto& a, const auto& b) {
      q.Step(); return a.creation != b.creation ? a.creation < b.creation : a.definition < b.definition;
    });
    // Indexed once per creator. No per-class scan through all initialization calls.
    std::vector<const ClassInitializerCall*> initializers;
    q.Reserve(initializers, values.graph.ops.size()); initializers.resize(values.graph.ops.size());
    for (const auto& call : values.initializers.calls) {
      q.Step(); q.operation=call.creation; q.Require(call.creation.value < initializers.size() && !initializers[call.creation.value], Error::kCreation);
      initializers[call.creation.value] = &call;
    }
    q.Reserve(result.tasks, values.classes.classes.size());
    // The family reader supplies the explicit creator DFS stack. This worklist
    // visits this creator's class occurrences in existing emission order only;
    // it never recursively reads member families during discovery.
    for (const auto& occurrence : values.classes.classes) {
      q.Step(); q.operation = occurrence.operation;
      q.Require(occurrence.operation.value < initializers.size(), Error::kCreation);
      q.Step(2 * (std::bit_width(result.definitions.size()) + 1));
      const auto first = std::lower_bound(result.definitions.begin(), result.definitions.end(), occurrence.operation,
          [](const auto& site, auto id) { return site.creation < id; });
      const auto last = std::upper_bound(first, result.definitions.end(), occurrence.operation,
          [](auto id, const auto& site) { return id < site.creation; });
      auto layout = q.Take<ClassTree>(BuildClassLayout(input.file, input.header, input.index,
          input.lookup, input.roles, input.classes, input.creator, occurrence.operation, input.identity,
          q.Left(), initializers[occurrence.operation.value], std::span(first,last)));
      q.Admit(layout.allocated_bytes); q.Step(layout.work);
      auto tree = std::allocate_shared<ClassTree>(ClassAllocator<ClassTree>{q},std::move(layout));
      result.tasks.push_back({occurrence.operation,std::move(tree),std::span(first,last),initializers[occurrence.operation.value]});
    }
    result.allocated_bytes=q.Cost().allocated_bytes; result.work=q.Cost().work;
    return result;
  });
}
std::variant<CreatorClassCost, CreatorClassFailure> VerifyCreatorClasses(
    const CreatorClassInput& input, const CreatorClasses& plan, analysis::SemanticBudget budget) {
  return Run(budget, [&](Quota& q) {
    const auto& graph=input.creator.values.graph;
    q.Require(plan.occurrence==input.occurrence && plan.identity==input.identity &&
        plan.creator==input.creator.input.Method() && plan.value_revision==graph.revision &&
        plan.cfg_revision==graph.cfg_revision && plan.tasks.size()==input.creator.values.classes.classes.size(),Error::kCreation);
    std::vector<ClassDefinitionSite> definitions;
    q.Reserve(definitions,input.creator.values.definitions.sites.size());
    definitions.insert(definitions.end(),input.creator.values.definitions.sites.begin(),input.creator.values.definitions.sites.end());
    std::sort(definitions.begin(),definitions.end(),[&](const auto& a,const auto& b){
      q.Step();return a.creation!=b.creation?a.creation<b.creation:a.definition<b.definition;
    });
    q.Step(definitions.size());q.Require(definitions==plan.definitions,Error::kCreation);
    std::vector<const ClassInitializerCall*> initializers;
    q.Reserve(initializers,graph.ops.size());initializers.resize(graph.ops.size());
    for(const auto& call:input.creator.values.initializers.calls) {
      q.Step();q.operation=call.creation;q.Require(call.creation.value<initializers.size() && !initializers[call.creation.value],Error::kCreation);
      initializers[call.creation.value]=&call;
    }
    std::vector<analysis::OpId> seen; q.Reserve(seen,plan.tasks.size());
    for (std::size_t i=0;i<plan.tasks.size();++i) {
      q.Step(); const auto& task=plan.tasks[i]; q.operation=task.operation;
      q.Require(task.operation==input.creator.values.classes.classes[i].operation && task.tree,Error::kCreation);
      const auto& tree=*task.tree;
      q.Require(!task.preparation && !task.owners && !tree.constructor && tree.members.empty() &&
          tree.omissions.empty() && !tree.initializer,Error::kCreation);
      q.Require(tree.identity==input.identity && tree.operation==task.operation &&
          tree.creation.creator==input.creator.input.Method() && task.operation.value<graph.ops.size(),Error::kCreation);
      q.Step(2*(std::bit_width(definitions.size())+1));
      const auto first=std::lower_bound(definitions.begin(),definitions.end(),task.operation,
          [](const auto& site,auto id){return site.creation<id;});
      const auto last=std::upper_bound(first,definitions.end(),task.operation,
          [](auto id,const auto& site){return id<site.creation;});
      q.Step(last-first);
      q.Require(task.definitions.size()==static_cast<std::size_t>(last-first) &&
          std::equal(first,last,task.definitions.begin()) && task.initializer==initializers[task.operation.value],Error::kCreation);
      auto expected=q.Take<ClassTree>(BuildClassLayout(input.file,input.header,input.index,input.lookup,
          input.roles,input.classes,input.creator,task.operation,input.identity,q.Left(),initializers[task.operation.value],std::span(first,last)));
      q.Admit(expected.allocated_bytes); q.Step(expected.work);
      q.Require(tree.creation.target==expected.creation.target && tree.creation.pc==expected.creation.pc &&
          tree.creation.creation==expected.creation.creation &&
          tree.member_sites==expected.member_sites && tree.callable==expected.callable && tree.name==expected.name,Error::kCreation);
      q.Require(tree.initializer_site==expected.initializer_site && tree.definitions.size()==expected.definitions.size(),Error::kCreation);
      for(std::size_t d=0;d<tree.definitions.size();++d) {
        q.Step();const auto& definition=tree.definitions[d];
        q.Require(definition.site==expected.definitions[d].site && !definition.bodies[0] &&
            !definition.bodies[1] && !definition.omissions[0] && !definition.omissions[1],Error::kCreation);
      }
      seen.push_back(task.operation);
    }
    std::sort(seen.begin(),seen.end(),[&](auto a,auto b){q.Step();return a<b;});
    for (std::size_t i=1;i<seen.size();++i) {q.Step();q.Require(seen[i-1]!=seen[i],Error::kCreation);}
    // Coverage is re-derived from live original-corresponding value operations,
    // not just agreement between two potentially shortened occurrence lists.
    std::size_t next=0;
    for(std::size_t i=0;i<graph.ops.size();++i) {
      q.Step();const auto& op=graph.ops[i];
      if(op.life!=analysis::ValueLife::kLive || !std::holds_alternative<analysis::ValueClassCreation>(op.operation)) continue;
      q.operation=analysis::OpId{static_cast<std::uint32_t>(i)};
      q.Require(next<seen.size() && seen[next++]==*q.operation,Error::kCreation);
    }
    q.Require(next==seen.size(),Error::kCreation);
    return q.Cost();
  });
}
std::variant<CreatorClassCost, CreatorClassFailure> PrepareCreatorClasses(
    const CreatorClassInput& input, CreatorClasses& plan, const GlobalScope* globals,
    FunctionOwnerId first_owner, RecoveryMode recovery, analysis::SemanticBudget budget) {
  return Run(budget,[&](Quota& q) {
    std::uint64_t next=first_owner.value;
    for (auto& task:plan.tasks) {
      q.Step();q.operation=task.operation; q.Require(next<=UINT32_MAX,Error::kResourceLimit);
      q.Require(!task.preparation && !task.owners,Error::kCreation);
      auto prepared=q.Take<ClassPreparation>(PrepareClassTree(input.file,input.header,input.index,input.lookup,
          input.roles,input.functions,input.classes,input.creator,task.operation,input.identity,globals,q.Left(),
          task.initializer,task.definitions,{static_cast<std::uint32_t>(next)},recovery));
      q.Admit(prepared.allocated_bytes);q.Step(prepared.work);
      const ClassOwnerRange range{next,prepared.next_owner};
      q.Require(ValidClassOwnerRange(range,next),Error::kCreation);
      task.owners=range;next=range.end;task.preparation.emplace(std::move(prepared));
    }
    return q.Cost();
  });
}
std::variant<CreatorClassCost, CreatorClassFailure> VerifyCreatorClassOwners(
    const CreatorClasses& plan, FunctionOwnerId first, analysis::SemanticBudget budget) {
  return Run(budget,[&](Quota& q) {
    std::uint64_t next=first.value;
    for(const auto& task:plan.tasks) {
      q.Step();q.operation=task.operation;
      q.Require(task.preparation && task.owners && ValidClassOwnerRange(*task.owners,next),Error::kCreation);
      const auto& prepared=*task.preparation;
      for(const auto& body:prepared.bodies) {
        q.Step();if(const auto* family=std::get_if<FamilyPreparation>(&body)) next+=family->MemberCount();
      }
      next+=task.initializer!=nullptr;
      q.Require(next==task.owners->end && next==prepared.next_owner,Error::kCreation);
    }
    return q.Cost();
  });
}
std::variant<CreatorClassCost, CreatorClassFailure> FinishCreatorClasses(
    const CreatorClassInput& input, CreatorClasses& plan, const GlobalScope* globals,
    FunctionOwnerId first_owner,const CaptureTree* parent,RecoveryMode recovery,analysis::SemanticBudget budget) {
  return Run(budget,[&](Quota& q) {
    auto checked=VerifyCreatorClassOwners(plan,first_owner,q.Left());
    if(const auto* failure=std::get_if<CreatorClassFailure>(&checked)) throw *failure;
    q.Step(std::get<CreatorClassCost>(checked).work);
    std::uint64_t next=first_owner.value;
    for (auto& task:plan.tasks) {
      q.Step();q.operation=task.operation;
      q.Require(task.owners && ValidClassOwnerRange(*task.owners,next) && task.preparation,Error::kCreation);
      auto definition=q.Take<ClassTree>(BuildClassTree(input.file,input.header,input.index,input.lookup,input.roles,
          input.functions,input.classes,input.creator,task.operation,input.identity,globals,q.Left(),task.initializer,
          task.definitions,recovery,{static_cast<std::uint32_t>(next)},parent,&*task.preparation));
      q.Require(definition.name==task.tree->name && definition.callable==task.tree->callable &&
          definition.member_sites==task.tree->member_sites && definition.DraftPrepared(),Error::kCreation);
      q.Admit(definition.allocated_bytes);q.Step(definition.work);
      next+=definition.constructor->Family().members.size();
      for(const auto& member:definition.members) if(member) next+=member->Family().members.size();
      for(const auto& site:definition.definitions) for(const auto& member:site.bodies)
        if(member) next+=member->Family().members.size();
      next+=definition.initializer.has_value();
      q.Require(next==task.owners->end,Error::kCreation);
      *task.tree=std::move(definition);
    }
    return q.Cost();
  });
}
}
