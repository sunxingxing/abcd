#include "creator_classes.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

#include "class_self_deferred.h"

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
          tree.member_sites==expected.member_sites && tree.callable==expected.callable && tree.name==expected.name && tree.sendable==expected.sendable,Error::kCreation);
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
    FunctionOwnerId first_owner, RecoveryMode recovery, analysis::SemanticBudget budget, ClassRootBinding root_binding) {
  return Run(budget,[&](Quota& q) {
    std::uint64_t next=first_owner.value;
    for (auto& task:plan.tasks) {
      q.Step();q.operation=task.operation; q.Require(next<=UINT32_MAX,Error::kResourceLimit);
      q.Require(!task.preparation && !task.owners,Error::kCreation);
      auto prepared=q.Take<ClassPreparation>(PrepareClassTree(input.file,input.header,input.index,input.lookup,
          input.roles,input.functions,input.classes,input.creator,task.operation,input.identity,globals,q.Left(),
          task.initializer,task.definitions,{static_cast<std::uint32_t>(next)},recovery,input.storage,root_binding));
      q.Admit(prepared.allocated_bytes);q.Step(prepared.work);
      const ClassOwnerRange range{next,prepared.next_owner};
      q.Require(ValidClassOwnerRange(range,next),Error::kCreation);
      task.owners=range;next=range.end;task.preparation.emplace(std::move(prepared));
    }
    return q.Cost();
  });
}
std::variant<CreatorClassDemands, CreatorClassFailure> CollectCreatorClassDemands(
    const CreatorClassInput& input, const CreatorClasses& plan, FamilyId creator,
    analysis::SemanticBudget budget) {
  return Run(budget,[&](Quota& q) {
    CreatorClassDemands result;
    q.Require(plan.occurrence==input.occurrence && creator.value==input.occurrence.member.value &&
        plan.identity==input.identity && plan.creator==input.creator.input.Method() &&
        plan.value_revision==input.creator.values.graph.revision &&
        plan.cfg_revision==input.creator.values.graph.cfg_revision,Error::kCreation);
    std::size_t count=0;
    for(const auto& task:plan.tasks) {
      q.Step();q.operation=task.operation;q.Require(task.preparation.has_value(),Error::kCreation);
      for(const auto& body:task.preparation->bodies) {
        q.Step();if(const auto* family=std::get_if<FamilyPreparation>(&body)) {
          q.Require(family->RootUses().size()<=SIZE_MAX-count,Error::kResourceLimit);
          count+=family->RootUses().size();
        }
      }
    }
    q.Reserve(result.demands,count);
    const auto& instructions=input.creator.input.Input().instructions;
    const auto& graph=input.creator.values.graph;
    for(const auto& task:plan.tasks) for(const auto& body:task.preparation->bodies) {
      q.Step();q.operation=task.operation;
      const auto* family=std::get_if<FamilyPreparation>(&body);if(!family) continue;
      const auto& context=family->Context();
      q.Require(context.ClassContext() && context.Identity()==input.identity,Error::kCreation);
      const auto* constructor=context.ConstructorSite();const auto* member=context.ClassSite();
      const auto* function=context.CreationSite();
      q.Require(constructor || member || function,Error::kCreation);
      q.Require((constructor?constructor->creator:member?member->creator:function->creator)==input.creator.input.Method(),Error::kCreation);
      const auto pc=constructor?constructor->pc:member?member->pc:function->pc;
      q.Step(std::bit_width(instructions.size())+1);
      const auto site=std::lower_bound(instructions.begin(),instructions.end(),pc,
          [](const auto& instruction,auto pc){return instruction.pc<pc;});
      q.Require(site!=instructions.end() && site->pc==pc,Error::kCreation);
      const auto id=static_cast<std::size_t>(site-instructions.begin());
      q.Require(id<graph.input.sites.size() && graph.input.sites[id],Error::kCreation);
      for(const auto& use:family->RootUses()) {
        q.Step();result.demands.push_back({creator,{*graph.input.sites[id],use.cell,use.domain,
            use.source?use.source:std::optional<CaptureOrigin>{{context.Target(),{}, {}}}}});
      }
    }
    result.allocated_bytes=q.Cost().allocated_bytes;result.work=q.Cost().work;return result;
  });
}
std::variant<CreatorClassCost, CreatorClassFailure> BindCreatorClasses(
    const corpus::AbcFile& file,const abc::Header& header,const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup,const FileFunction& creator,const CaptureStorage* storage,
    CreatorClasses& plan,analysis::SemanticBudget budget) {
  return Run(budget,[&](Quota& q) {
    q.Require(plan.creator==creator.input.Method() && plan.value_revision==creator.values.graph.revision &&
        plan.cfg_revision==creator.values.graph.cfg_revision,Error::kCreation);
    for(auto& task:plan.tasks) {
      q.Step();q.operation=task.operation;q.Require(task.preparation.has_value(),Error::kCreation);
      for(auto& body:task.preparation->bodies) if(auto* family=std::get_if<FamilyPreparation>(&body)) {
        q.Step();auto bound=family->BindCreator(file,header,index,lookup,creator,storage,q.Left());
        if(const auto* error=std::get_if<FamilyPrepareFailure>(&bound))throw CreatorClassFailure{*error,q.operation};
        const auto cost=std::get<FamilyCellCost>(bound);q.Admit(cost.allocated_bytes);q.Step(cost.work);
      }
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
        q.Step();if(const auto* family=std::get_if<FamilyPreparation>(&body)) next+=family->OwnerCount();
      }
      next+=task.initializer!=nullptr;
      q.Require(next==task.owners->end && next==prepared.next_owner,Error::kCreation);
    }
    return q.Cost();
  });
}
std::variant<CreatorClassCost, CreatorClassFailure> VerifyFinishedClassOwners(
    const CreatorClasses& plan, FunctionOwnerId first, analysis::SemanticBudget budget) {
  return Run(budget,[&](Quota& q) {
    std::uint64_t next=first.value;
    for(const auto& task:plan.tasks) {
      q.Step();q.operation=task.operation;
      q.Require(task.tree && task.owners && ValidClassOwnerRange(*task.owners,next) && task.tree->DraftPrepared(),Error::kCreation);
      const auto range=*task.owners;const auto count=range.end-range.begin;
      q.Require(count<=SIZE_MAX,Error::kResourceLimit);
      std::vector<const FunctionTree*> stack;q.Reserve(stack,static_cast<std::size_t>(count));
      std::vector<std::uint8_t> seen;q.Reserve(seen,static_cast<std::size_t>(count));seen.resize(count);
      const auto add_tree=[&](const FunctionTree& tree){
        q.Step();q.Require(tree.identity==plan.identity && tree.owner.value>=range.begin && tree.owner.value<range.end,Error::kCreation);
        const auto id=tree.owner.value-range.begin;q.Require(!seen[id] && stack.size()<count,Error::kCreation);
        seen[id]=1;stack.push_back(&tree);
      };
      const auto add_class=[&](const ClassTree& tree){
        q.Require(tree.DraftPrepared() && tree.identity==plan.identity,Error::kCreation);
        const auto family=[&](const PreparedFamily& p){for(const auto& member:p.Family().members)add_tree(member.tree);};
        family(*tree.constructor);for(const auto& member:tree.members){q.Step();if(member)family(*member);}
        for(const auto& definition:tree.definitions)for(const auto& member:definition.bodies){q.Step();if(member)family(*member);}
        if(tree.initializer)add_tree(tree.initializer->tree);
      };
      add_class(*task.tree);
      while(!stack.empty()){
        const auto* tree=stack.back();stack.pop_back();q.Step();
        for(const auto& node:tree->expressions){q.Step();if(const auto* created=std::get_if<ClassExpr>(&node.expression)){
          q.Require(created->definition && created->definition->creation.creator==tree->method,Error::kCreation);add_class(*created->definition);
        }}
      }
      for(auto value:seen){q.Step();q.Require(value,Error::kCreation);}
      next=range.end;
    }
    return q.Cost();
  });
}
std::variant<CreatorClassCost, CreatorClassFailure> FinishCreatorClasses(
    const CreatorClassInput& input, CreatorClasses& plan, const GlobalScope* globals,
    FunctionOwnerId first_owner,const CaptureTree* parent,RecoveryMode recovery,analysis::SemanticBudget budget) {
  return Run(budget,[&](Quota& q) {
    q.Require(plan.occurrence==input.occurrence && plan.identity==input.identity &&
        plan.creator==input.creator.input.Method() && plan.value_revision==input.creator.values.graph.revision &&
        plan.cfg_revision==input.creator.values.graph.cfg_revision,Error::kCreation);
    auto checked=VerifyCreatorClassOwners(plan,first_owner,q.Left());
    if(const auto* failure=std::get_if<CreatorClassFailure>(&checked)) throw *failure;
    q.Step(std::get<CreatorClassCost>(checked).work);
    std::uint64_t next=first_owner.value;
    for (auto& task:plan.tasks) {
      q.Step();q.operation=task.operation;
      q.Require(task.owners && ValidClassOwnerRange(*task.owners,next) && task.preparation,Error::kCreation);
      std::optional<ClassStaticFields> certified_initializer;
      std::optional<ClassSelfInitializationDiagnostic> class_diagnostic;
      if (parent && input.creator.class_self &&
          input.creator.class_self->initialization.Evidence().call.creation ==
              task.operation) {
        auto diagnostic = BindDeferredClassSelf(
            input.file, input.header, input.index, input.lookup, input.roles,
            input.classes, input.creator, *input.creator.class_self, *task.tree,
            {static_cast<std::uint32_t>(task.owners->end - 1)}, *parent,
            input.storage, q.Left());
        auto& retained = *input.creator.class_self;
        if (retained.flow && retained.initializer.tree.class_scope &&
            diagnostic.recovery &&
            diagnostic.recovery->kind ==
                ClassSelfInitializationError::kRecoveryGate &&
            diagnostic.recovery->projection ==
                ClassSelfInitializationFailure::Projection::
                    kClassInitializationPorts) {
          q.Admit(retained.enclosure_cost.bytes);
          q.Step(retained.enclosure_cost.work);
          certified_initializer.emplace(ClassStaticFields{
              retained.initialization.Evidence().call,
              std::move(retained.context), std::move(retained.initializer.tree),
              retained.initializer_family.members.front().function.Record(),
              std::move(*retained.flow)});
          task.preparation->layout.class_self =
              std::move(task.tree->class_self);
          class_diagnostic = diagnostic;
        } else {
          FamilyFileFailure refused{ClassSelfError::kRecoveryGate};
          refused.method = diagnostic.initializer;
          refused.root = input.occurrence.family_root;
          if (diagnostic.recovery &&
              diagnostic.recovery->kind ==
                  ClassSelfInitializationError::kResourceLimit)
            refused.cause = ClassSelfError::kResourceLimit;
          else if (diagnostic.recovery &&
                   diagnostic.recovery->kind ==
                       ClassSelfInitializationError::kWorkLimit)
            refused.cause = ClassSelfError::kWorkLimit;
          refused.class_self_initialization = std::move(diagnostic);
          throw CreatorClassFailure{
              ClassTreeFailure{FamilyPrepareFailure{std::move(refused)}},
              task.operation};
        }
      }
      auto completed = BuildClassTree(
          input.file, input.header, input.index, input.lookup, input.roles,
          input.functions, input.classes, input.creator, task.operation,
          input.identity, globals, q.Left(), task.initializer, task.definitions,
          recovery, {static_cast<std::uint32_t>(next)}, parent,
          &*task.preparation, input.storage,
          certified_initializer ? &*certified_initializer : nullptr);
      if (class_diagnostic)
        if (const auto* error = std::get_if<ClassTreeFailure>(&completed))
          if (const auto* prepare = std::get_if<FamilyPrepareFailure>(error))
            if (const auto* family =
                    std::get_if<FamilyTreeFailure>(&prepare->cause);
                family && family->abi_lengths && family->method) {
              auto diagnostic = std::move(*class_diagnostic);
              diagnostic.recovery->kind =
                  ClassSelfInitializationError::kRecoveryGate;
              diagnostic.recovery->projection =
                  ClassSelfInitializationFailure::Projection::kMemberAbi;
              diagnostic.recovery->location = {};
              diagnostic.recovery->location.method = *family->method;
              diagnostic.recovery->abi_lengths =
                  ClassSelfInitializationFailure::AbiLengths{
                      *family->method, family->abi_lengths->declared,
                      family->abi_lengths->signature};
              FamilyFileFailure refused{ClassSelfError::kRecoveryGate};
              refused.method = *family->method;
              refused.root = input.occurrence.family_root;
              refused.class_self_initialization = std::move(diagnostic);
              throw CreatorClassFailure{
                  ClassTreeFailure{FamilyPrepareFailure{std::move(refused)}},
                  task.operation};
            }
      auto definition = q.Take<ClassTree>(std::move(completed));
      q.Require(definition.name==task.tree->name && definition.callable==task.tree->callable &&
          definition.member_sites==task.tree->member_sites && definition.DraftPrepared(),Error::kCreation);
      q.Admit(definition.allocated_bytes);q.Step(definition.work);
      next+=definition.constructor->Family().OwnerCount();
      for(const auto& member:definition.members) if(member) next+=member->Family().OwnerCount();
      for(const auto& site:definition.definitions) for(const auto& member:site.bodies)
        if(member) next+=member->Family().OwnerCount();
      next+=definition.initializer.has_value();
      q.Require(next==task.owners->end,Error::kCreation);
      *task.tree=std::move(definition);
    }
    auto owners=VerifyFinishedClassOwners(plan,first_owner,q.Left());
    if(const auto* failure=std::get_if<CreatorClassFailure>(&owners))throw *failure;
    const auto cost=std::get<CreatorClassCost>(owners);q.Admit(cost.allocated_bytes);q.Step(cost.work);
    return q.Cost();
  });
}
}
