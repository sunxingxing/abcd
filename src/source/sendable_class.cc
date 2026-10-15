#include "sendable_class.h"
#include "sendable_types.h"
#include <bit>
namespace abcd::source {
bool PrimitiveSendableType(abc::SendableTypeMask type) {
  constexpr auto unsupported=static_cast<unsigned>(abc::SendableFieldType::kTypeReference) |
      static_cast<unsigned>(abc::SendableFieldType::kGeneric);
  return type.bits && !(type.bits & unsupported);
}
std::optional<SendableFailure> VerifySendableClass(const ClassTree& tree,bool finished,analysis::SemanticBudget budget,std::uint64_t* work) {
  struct Cost {std::uint64_t initial;const analysis::SemanticBudget& budget;std::uint64_t* out;
    ~Cost(){if(out)*out=initial-budget.work;}} cost{budget.work,budget,work};
  const auto kind=tree.creation.creation.kind;
  if(kind==abc::ClassKind::kOrdinary) {
    if(!tree.sendable) {
      if(!finished || !tree.constructor)return {};
      const auto& family=tree.constructor->Family();
      if(family.root.value<family.members.size() && !family.members[family.root.value].tree.sendable)return {};
    }
  }
  SendableFailure at;at.method=tree.creation.creator;at.pc=tree.creation.pc;
  at.constructor=tree.creation.target;at.pool=tree.creation.creation.literal;
  const auto fail=[&](SendableError e){at.kind=e;return std::optional{at};};
  const auto step=[&](std::uint64_t n=1){if(n>budget.work)return false;budget.work-=n;return true;};
  if(!step())return fail(SendableError::kWorkLimit);
  if(kind!=abc::ClassKind::kSendable || !tree.sendable)return fail(SendableError::kCorrespondence);
  const auto& source=*tree.sendable;at.literal=source.literal;at.field_types=source.members.field_types;
  if(SendableOutput(tree.identity.contract)!=SendableOutputPolicy::kArkDirectiveV1)return fail(SendableError::kOutputPolicy);
  if(source.field_failure) {
    at.field=source.field_failure->field;at.cause=source.field_failure->kind;at.mask=source.field_failure_mask;
    return fail(SendableError::kFieldTypes);
  }
  if(source.members.kind!=kind || !source.members.field_types ||
      source.members.properties.size()!=tree.member_sites.size())
    return fail(SendableError::kCorrespondence);
  if(tree.callable!=CallableKind::kBaseConstructor)return fail(SendableError::kHeritage);
  if(tree.initializer_site || !tree.definitions.empty())return fail(SendableError::kPreparationGate);
  for(std::size_t i=0;i<source.fields.fields.size();++i) {
    at.field=static_cast<std::uint32_t>(i);at.mask=source.fields.fields[i].type.bits;
    if(!step(std::bit_width(source.field_names.size())+1))return fail(SendableError::kWorkLimit);
    const auto* name=source.FieldName(i);
    if(!name)return fail(SendableError::kCorrespondence);
    // Class-field grammar, not IdentifierName/member-access grammar. The
    // pinned dynamic frontend rejects these field keys (including quotes).
    if(*name==u"constructor" || (*name==u"prototype" &&
        source.fields.fields[i].placement==abc::SendableFieldPlacement::kStatic))
      return fail(SendableError::kTypeSpelling);
    if(!PrimitiveSendableType(source.fields.fields[i].type) &&
        source.fields.fields[i].type.bits != static_cast<unsigned>(abc::SendableFieldType::kGeneric))return fail(SendableError::kTypeSpelling);
    // The pinned frontend adds a static-initializer member for static storage.
    // Its timing/literal correspondence is not part of this closed-instance
    // projection. Do not generate an unmatched initializer from a descriptor.
    if(source.fields.fields[i].placement==abc::SendableFieldPlacement::kStatic)
      return fail(SendableError::kPreparationGate);
  }
  at.field.reset();at.mask.reset();
  if(!finished)return {};
  if(!tree.BodiesPrepared() || tree.initializer || !tree.omissions.empty())return fail(SendableError::kPreparationGate);
  const auto types=VerifySendableTypes(tree,budget);
  if(const auto* failure=std::get_if<SendableFailure>(&types))return *failure;
  if(!step(std::get<SendableTypeCost>(types).work))return fail(SendableError::kWorkLimit);
  const auto& constructor=tree.constructor->Family();
  if(constructor.root.value>=constructor.members.size())return fail(SendableError::kCorrespondence);
  const auto& root=constructor.members[constructor.root.value].tree;
  const SendableConstructorDirective expected{tree.creation.target,tree.creation.creator,tree.creation.pc,
      tree.creation.creation.constructor,tree.creation.creation.literal};
  if(root.sendable!=expected || root.callable!=CallableKind::kBaseConstructor || root.ImplicitConstructor())
    return fail(SendableError::kCorrespondence);
  const auto closed=[&](const PreparedFamily& prepared,bool ctor)->std::optional<SendableFailure> {
    const auto& family=prepared.Family();
    if(family.root.value>=family.members.size())return fail(SendableError::kCorrespondence);
    const auto& body=family.members[family.root.value].tree;
    if(body.module_domain!=analysis::ModuleDomain::kSendable)return fail(SendableError::kEnvironment);
    if((family.root_capture && !family.root_capture->targets.empty()) || !body.captures.empty())return fail(SendableError::kCapture);
    for(const auto& member:family.members) {
      if(!step())return fail(SendableError::kWorkLimit);
      if(!member.tree.recovery_issues.empty())return fail(SendableError::kPreparationGate);
      for(const auto& alias:member.tree.modules) {
        if(!step())return fail(SendableError::kWorkLimit);
        if(alias.local || alias.access!=ModuleAccess::kReadOnly || alias.symbol.value>=member.tree.symbols.size() ||
            member.tree.symbols[alias.symbol.value].kind!=BindingKind::kModule)return fail(SendableError::kEnvironment);
      }
      if(member.tree.sendable && (!ctor || member.tree.owner!=body.owner))return fail(SendableError::kCorrespondence);
    }
    return {};
  };
  if(auto error=closed(*tree.constructor,true))return error;
  for(std::size_t i=0;i<tree.members.size();++i) {
    const auto& member=tree.members[i];
    if(!step())return fail(SendableError::kWorkLimit);
    if(!member)return fail(SendableError::kPreparationGate);
    if(auto error=closed(*member,false))return error;
    const auto* method=std::get_if<abc::ClassMethod>(&source.members.properties[i].value);
    const auto& family=member->Family();
    if(!method || family.members[family.root.value].tree.method!=method->target)
      return fail(SendableError::kCorrespondence);
  }
  return {};
}
}
