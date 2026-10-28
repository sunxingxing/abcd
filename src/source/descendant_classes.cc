#include "descendant_classes.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include "module_aliases.h"
namespace abcd::source {
namespace {
class Preparation {
 public:
  Preparation(const corpus::AbcFile& f,const abc::Header& h,const abc::MetadataIndex& i,
      const abc::MethodLookup& l,const FileFamily& family,const GlobalScope* globals,
      RecoveryMode recovery,analysis::SemanticBudget budget)
      : file(f),header(h),index(i),lookup(l),family(family),globals(globals),recovery(recovery),initial(budget),left(budget) {}
  FamilyId owner{};
  std::optional<analysis::OpId> operation;
  [[noreturn]] void Fail(FamilyCreationCheck check) const {
    const auto& function=family.members[owner.value].function;
    FamilyFileFailure cause{FamilyFileError::kCreation,function.Method()};
    FamilyCreationEvidence evidence{check,operation ? OperationLocation(function.Graph(),*operation,function.Method()):TreeLocation{}};
    evidence.location.method=function.Method(); evidence.occurrence=FamilyOccurrenceId{owner.value};
    evidence.root=family.members.front().function.Method();evidence.projected_class=operation;
    if(operation && operation->value<function.Graph().ops.size()){
      const auto& op=function.Graph().ops[operation->value];
      if(op.origin.value<function.Graph().origins.size())evidence.instruction=analysis::InstructionId{function.Graph().origins[op.origin.value].site.value};
      if(const auto* created=std::get_if<analysis::ValueClassCreation>(&op.operation)){
        evidence.kind=FamilyCreationKind::kClass;evidence.constructor=created->constructor;evidence.literal=created->literal;
      }
    }
    if(const auto& incoming=family.members[owner.value].incoming;incoming){
      const auto& creator=family.members[incoming->parent.value].function;
      const auto& graph=creator.Graph();
      if(incoming->site.value<graph.input.sites.size() && graph.input.sites[incoming->site.value])
        evidence.descendant_source=OperationLocation(graph,*graph.input.sites[incoming->site.value],creator.Method());
    }
    cause.family_creation=evidence;cause.root=evidence.root;cause.creation_pc=evidence.location.pc;
    throw FamilyTreeFailure{owner,std::move(cause)};
  }
  void Require(bool b,FamilyCreationCheck c) const {if(!b)Fail(c);}
  void Step(std::uint64_t n=1) {if(n>left.work)throw FamilyTreeFailure{owner,FamilyTreeError::kWorkLimit};left.work-=n;}
  void Admit(std::size_t n,std::size_t w=1) {if(n>left.bytes/w)throw FamilyTreeFailure{owner,FamilyTreeError::kResourceLimit};left.bytes-=n*w;}
  template<class T> void Reserve(std::vector<T>& v,std::size_t n){Admit(n,sizeof(T));v.reserve(n);Admit(v.capacity()-n,sizeof(T));}
  template<class T> void Account(const T& v){Admit(v.allocated_bytes);Step(v.work);}
  template<class E> [[noreturn]] void Error(const E& e) {
    if constexpr(std::is_same_v<E,FamilyTreeFailure>) throw e;
    else if constexpr(std::is_same_v<E,CreatorClassFailure>){if(e.operation) operation=e.operation;Error(e.cause);}
    else if constexpr(requires{std::variant_size<E>::value;}) std::visit([&](const auto& x){Error(x);},e);
    else if constexpr(std::is_same_v<E,FamilyPrepareFailure>) Error(e.cause);
    else if constexpr(std::is_constructible_v<decltype(FamilyTreeFailure::cause),E>) throw FamilyTreeFailure{owner,e};
    else if constexpr(std::is_constructible_v<decltype(FamilyFileFailure::cause),E>)
      throw FamilyTreeFailure{owner,FamilyFileFailure{e,family.members[owner.value].function.Method()}};
    else if constexpr(requires{e.error;})Error(e.error);
    else if constexpr(requires{e.cause;})Error(e.cause);
    else {
      if constexpr(std::is_enum_v<E>) {
        if constexpr(requires{E::kResourceLimit;})if(e==E::kResourceLimit)throw FamilyTreeFailure{owner,FamilyTreeError::kResourceLimit};
        if constexpr(requires{E::kWorkLimit;})if(e==E::kWorkLimit)throw FamilyTreeFailure{owner,FamilyTreeError::kWorkLimit};
      }
      Fail(FamilyCreationCheck::kClassPreparationInput);
    }
    std::terminate();
  }
  template<class E> void Resources(const E& error) const {
    if constexpr(std::is_enum_v<E>){
      if constexpr(requires{E::kResourceLimit;})if(error==E::kResourceLimit)throw FamilyTreeFailure{owner,FamilyTreeError::kResourceLimit};
      if constexpr(requires{E::kWorkLimit;})if(error==E::kWorkLimit)throw FamilyTreeFailure{owner,FamilyTreeError::kWorkLimit};
    }else if constexpr(requires{std::variant_size<E>::value;})std::visit([&](const auto& e){Resources(e);},error);
    else {
      if constexpr(requires{error.kind;})Resources(error.kind);
      if constexpr(requires{error.cause;})Resources(error.cause);
      if constexpr(requires{error.error;})Resources(error.error);
    }
  }
  template<class T,class E>T Take(std::variant<T,E> v){if(auto* e=std::get_if<E>(&v))Error(*e);return std::get<T>(std::move(v));}
  CreatorClassInput Input() const {
    const auto& function=family.members[owner.value].function;
    Require(family.class_roles && family.class_functions && family.class_sites,
            FamilyCreationCheck::kClassPreparationInput);
    return {file,
            header,
            index,
            lookup,
            *family.class_roles,
            *family.class_functions,
            *family.class_sites,
            function.Phase() ? ClassCreator{*function.Phase()}
                             : ClassCreator{function.Normal()},
            function.Identity(),
            {family.members.front().function.Method(),
             family.first_owner,
             {owner.value}},
            function.Storage()};
  }
  void Reference(DescendantClasses& result,std::u16string_view name) {
    Step(name.size()+1);
    if(result.references.size()==result.references.capacity()){
      const auto n=result.references.size();if(n>SIZE_MAX/2)throw FamilyTreeFailure{owner,FamilyTreeError::kResourceLimit};
      Reserve(result.references,n?2*n:1);
    }
    Admit(name.size()+1,sizeof(char16_t));
    result.references.push_back({std::u16string(name),family.Owner(owner)});
    Admit(result.references.back().name.capacity()-name.size(),sizeof(char16_t));
  }
  // A class-family free name is reserved in its real creator's subtree. Owner
  // identity is not borrowed from an unrelated class's source namespace.
  void References(DescendantClasses& result,const FamilyPreparation& prepared) {
    const auto& input=prepared.Input();
    for(std::size_t m=0;m<input.members.size();++m){
      Step();const auto& member=input.members[m];const auto& function=member.function;
      const auto& context=m?*member.context:prepared.Context();
      const auto* phase=function.Phase();
      auto syntax=Take<ResolvedSyntax>(ResolveFunctionSyntax(file,header,index,context,
          phase?phase->OriginalBody():function.Body(),function.Graph(),input.Owner({static_cast<std::uint32_t>(m)}),left,
          function.Defaults()?function.Defaults()->Strings():std::span<const SignatureString>{},function.Cells()));
      Account(syntax);
      const auto& graph=function.Graph();
      for(std::size_t o=0;o<graph.ops.size();++o){Step();if(graph.ops[o].life!=analysis::ValueLife::kLive ||
          !std::holds_alternative<analysis::ValueGlobalRead>(graph.ops[o].operation))continue;
        Require(o<syntax.operations.size() && std::holds_alternative<NameId>(syntax.operations[o]),FamilyCreationCheck::kClassPreparationInput);
        const auto id=std::get<NameId>(syntax.operations[o]);Require(id.value<syntax.syntax.names.size(),FamilyCreationCheck::kClassPreparationInput);
        Reference(result,syntax.syntax.names[id.value]);
      }
      const auto* aliases=phase?phase->Phases().Modules():nullptr;
      std::optional<ModuleAliases> normal;
      if(!phase && !function.Normal().values.modules.pairs.empty()){
        Require(globals && globals->module_enclosure,FamilyCreationCheck::kClassPreparationInput);
        const auto& enclosure=*globals->module_enclosure;
        normal=Take<ModuleAliases>(BindModuleAliases(file,header,index,function.Normal().input,function.Normal().values,
            enclosure.Context(),enclosure.Owner(),{},left,ModuleAccess::kMutable));Account(*normal);aliases=&*normal;
      }
      if(aliases) for(const auto& name:aliases->names)Reference(result,name);
    }
  }
  DescendantClasses Build(bool entry){
    DescendantClasses result;
    std::size_t count=0;
    for(std::size_t i=entry?1:0;i<family.members.size();++i){Step();count+=!family.members[i].classes.empty();}
    result.owner_end=std::max(family.next_class_owner,std::uint64_t{family.first_owner.value}+family.members.size());
    if(!count){result.work=initial.work-left.work;return result;}
    Reserve(result.creators,family.members.size());result.creators.resize(family.members.size());
    // FileFamily's explicit DFS has already fixed all ordinary occurrences.
    // The work stack retains creator identities; no recursive class discovery.
    std::vector<FamilyId> pending;Reserve(pending,count);
    for(std::size_t i=family.members.size();i-->(entry?1u:0u);){Step();if(!family.members[i].classes.empty())pending.push_back({static_cast<std::uint32_t>(i)});}
    std::uint64_t next=std::max(family.next_class_owner,std::uint64_t{family.first_owner.value}+family.members.size());
    while(!pending.empty()){
      owner=pending.back();pending.pop_back();Step();operation=family.members[owner.value].classes.front().creation;const auto input=Input();
      auto plan=Take<CreatorClasses>(DiscoverCreatorClasses(input,left));Account(plan);
      Account(Take<CreatorClassCost>(VerifyCreatorClasses(input,plan,left)));
      for(const auto& task:plan.tasks){Step();operation=task.operation;
      }
      if(next>UINT32_MAX)throw FamilyTreeFailure{owner,FamilyTreeError::kResourceLimit};
      Account(Take<CreatorClassCost>(PrepareCreatorClasses(input,plan,globals,{static_cast<std::uint32_t>(next)},recovery,left,ClassRootBinding::kRequirements)));
      auto summary=Take<CreatorClassDemands>(CollectCreatorClassDemands(input,plan,owner,left));Account(summary);
      // Creator declarations are never mutated here: a requirement is admitted
      // only when the existing creator storage binds it unchanged.
      if(!summary.demands.empty())operation=summary.demands.front().demand.creation;
      auto binding=BindCreatorClasses(file,header,index,lookup,input.creator,input.storage,plan,left);
      if(const auto* error=std::get_if<CreatorClassFailure>(&binding)){
        Resources(*error);
        if(!summary.demands.empty())Fail(FamilyCreationCheck::kClassCaptureRequirements);
        Error(*error);
      }
      Account(std::get<CreatorClassCost>(binding));
      for(const auto& task:plan.tasks){Step();operation=task.operation;
        for(const auto& body:task.preparation->bodies)if(const auto* p=std::get_if<FamilyPreparation>(&body)){
          Require(!p->RootCapture() || p->RootCapture()->deferred.empty(),FamilyCreationCheck::kClassDeferredDeclaration);
        }
      }
      // Nonempty demands are admitted only after BindCreatorClasses has bound
      // every class member against the creator's actual storage (above) and no
      // body needs a deferred declaration (checked just before). A demand the
      // creator cannot satisfy still fails binding as kClassCaptureRequirements.
      for(const auto& task:plan.tasks)for(const auto& body:task.preparation->bodies)
        if(const auto* prepared=std::get_if<FamilyPreparation>(&body))References(result,*prepared);
      Account(Take<CreatorClassCost>(VerifyCreatorClassOwners(plan,{static_cast<std::uint32_t>(next)},left)));
      if(!plan.tasks.empty())next=plan.tasks.back().owners->end;
      result.creators[owner.value].emplace(std::move(plan));
    }
    result.owner_end=next;
    result.allocated_bytes=initial.bytes-left.bytes;result.work=initial.work-left.work;return result;
  }
  CreatorClassCost Finish(FamilyId id,CreatorClasses& plan,const CaptureTree& parent){
    owner=id;const auto input=Input();Require(!plan.tasks.empty() && plan.tasks.front().owners,FamilyCreationCheck::kClassCompletion);
    const auto first=plan.tasks.front().owners->begin;Require(first<=UINT32_MAX,FamilyCreationCheck::kClassCompletion);
    Account(Take<CreatorClassCost>(FinishCreatorClasses(input,plan,globals,{static_cast<std::uint32_t>(first)},&parent,recovery,left)));
    return {initial.bytes-left.bytes,initial.work-left.work};
  }
 private:
  const corpus::AbcFile& file;const abc::Header& header;const abc::MetadataIndex& index;const abc::MethodLookup& lookup;
  const FileFamily& family;const GlobalScope* globals;RecoveryMode recovery;analysis::SemanticBudget initial,left;
};
}
std::variant<DescendantClasses,FamilyTreeFailure> PrepareDescendantClasses(const corpus::AbcFile& f,const abc::Header& h,
    const abc::MetadataIndex& i,const abc::MethodLookup& l,const FileFamily& family,bool entry,const GlobalScope* globals,
    RecoveryMode recovery,analysis::SemanticBudget budget){
  try{return Preparation(f,h,i,l,family,globals,recovery,budget).Build(entry);}
  catch(const FamilyTreeFailure& e){return e;}catch(const std::bad_alloc&){return FamilyTreeFailure{{0},FamilyTreeError::kResourceLimit};}
  catch(const std::length_error&){return FamilyTreeFailure{{0},FamilyTreeError::kResourceLimit};}
}
std::variant<CreatorClassCost,FamilyTreeFailure> FinishDescendantClasses(const corpus::AbcFile& f,const abc::Header& h,
    const abc::MetadataIndex& i,const abc::MethodLookup& l,const FileFamily& family,FamilyId owner,CreatorClasses& plan,
    const GlobalScope* globals,const CaptureTree& parent,RecoveryMode recovery,analysis::SemanticBudget budget){
  if(owner.value>=family.members.size())return FamilyTreeFailure{owner,FamilyTreeError::kParent};
  try{return Preparation(f,h,i,l,family,globals,recovery,budget).Finish(owner,plan,parent);}
  catch(const FamilyTreeFailure& e){return e;}catch(const std::bad_alloc&){return FamilyTreeFailure{owner,FamilyTreeError::kResourceLimit};}
  catch(const std::length_error&){return FamilyTreeFailure{owner,FamilyTreeError::kResourceLimit};}
}
}
