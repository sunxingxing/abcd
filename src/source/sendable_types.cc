#include "sendable_types.h"
#include "abcd/source/global_reference.h"
#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
struct Result { std::vector<SendableTypeSpelling> types; SendableTypeCost cost; };
Result Derive(const ClassTree& tree,analysis::SemanticBudget budget) {
  const auto initial=budget;
  SendableFailure at;at.method=tree.creation.creator;at.pc=tree.creation.pc;at.constructor=tree.creation.target;
  const auto fail=[&](SendableError kind)->void{at.kind=kind;throw at;};
  const auto step=[&](std::uint64_t n=1){if(n>budget.work)fail(SendableError::kWorkLimit);budget.work-=n;};
  const auto reserve=[&]<class T>(std::vector<T>& v,std::size_t n){
    if(n>budget.bytes/sizeof(T))fail(SendableError::kResourceLimit);budget.bytes-=n*sizeof(T);
    v.reserve(n);if(v.capacity()-n>budget.bytes/sizeof(T))fail(SendableError::kResourceLimit);budget.bytes-=(v.capacity()-n)*sizeof(T);
  };
  Result result;
  if(!tree.sendable)return result;
  const auto& fields=tree.sendable->fields.fields;
  std::size_t count=0;for(const auto& field:fields){step();count+=field.type.bits==static_cast<unsigned>(abc::SendableFieldType::kGeneric);}
  if(!count){result.cost.work=initial.work-budget.work;return result;}
  if(!tree.constructor)fail(SendableError::kPreparationGate);
  const auto& family=tree.constructor->Family();if(family.root.value>=family.members.size())fail(SendableError::kCorrespondence);
  const auto& body=family.members[family.root.value].tree;
  struct Path {std::optional<SymbolId> root;std::uint32_t depth=0;};
  std::vector<Path> paths;reserve(paths,body.expressions.size());paths.resize(body.expressions.size());
  std::vector<std::optional<ModuleSymbol>> imports;reserve(imports,body.symbols.size());imports.resize(body.symbols.size());
  for(const auto& alias:body.modules){step();if(alias.symbol.value>=imports.size())fail(SendableError::kBinding);
    if(!alias.local&&alias.access==ModuleAccess::kReadOnly)imports[alias.symbol.value]=alias.target;
  }
  struct Field {std::u16string_view name;std::uint32_t ordinal;};
  std::vector<Field> ordered;reserve(ordered,count);reserve(result.types,count);
  for(std::size_t i=0;i<fields.size();++i){step(std::bit_width(tree.sendable->field_names.size())+1);
    if(fields[i].type.bits!=static_cast<unsigned>(abc::SendableFieldType::kGeneric))continue;
    const auto* name=tree.sendable->FieldName(i);if(!name)fail(SendableError::kCorrespondence);
    ordered.push_back({*name,static_cast<std::uint32_t>(i)});
  }
  const auto less=[&](std::u16string_view a,std::u16string_view b){step(std::min(a.size(),b.size())+1);return a<b;};
  std::sort(ordered.begin(),ordered.end(),[&](auto a,auto b){return less(a.name,b.name);});
  std::vector<std::optional<SendableTypeSpelling>> found;reserve(found,count);found.resize(count);
  for(std::size_t i=0;i<body.expressions.size();++i){step();const auto& expr=body.expressions[i].expression;
    if(const auto* symbol=std::get_if<SymbolExpr>(&expr)) {
      if(symbol->symbol.value<imports.size()&&imports[symbol->symbol.value]&&symbol->domain==analysis::ModuleDomain::kSendable)
        paths[i].root=symbol->symbol;
    } else if(const auto* member=std::get_if<MemberExpr>(&expr)) {
      const auto* name=std::get_if<NameId>(&member->key);
      if(member->base.value<i&&paths[member->base.value].root&&name&&name->value<body.names.size()&&member->lookup==analysis::PropertyLookup::kObject){
        step(body.names[name->value].size());
        if(IsSourceIdentifier(std::u16string_view(body.names[name->value])))paths[i]={paths[member->base.value].root,paths[member->base.value].depth+1};
      }
    }
    const auto* assign=std::get_if<AssignmentExpr>(&expr);if(!assign)continue;
    const auto* target=std::get_if<MemberTarget>(&assign->target);if(!target)continue;
    const auto* name=std::get_if<NameId>(&target->key);
    if(!name||name->value>=body.names.size()||target->base.value>=i||assign->value.value>=i)continue;
    const auto* self=std::get_if<ThisExpr>(&body.expressions[target->base.value].expression);
    if(!self||self->owner!=body.owner)continue;
    const auto key=std::u16string_view(body.names[name->value]);
    const auto field=std::lower_bound(ordered.begin(),ordered.end(),key,[&](auto a,auto b){return less(a.name,b);});
    if(field==ordered.end()||less(key,field->name))continue;
    at.field=field->ordinal;at.mask=fields[field->ordinal].type.bits;
    const auto* value=std::get_if<ConstructExpr>(&body.expressions[assign->value.value].expression);
    if(!value||value->constructor.value>=assign->value.value)fail(SendableError::kTypeSpelling);
    const auto path=paths[value->constructor.value];if(!path.root)fail(SendableError::kTypeSpelling);
    const auto slot=static_cast<std::size_t>(field-ordered.begin());
    if(found[slot])fail(SendableError::kTypeSpelling); // Ambiguous field type spelling is an implementation gap.
    found[slot]=SendableTypeSpelling{field->ordinal,{static_cast<std::uint32_t>(i)},value->constructor,*path.root,*imports[path.root->value],path.depth};
  }
  for(std::size_t i=0;i<count;++i){step();at.field=ordered[i].ordinal;at.mask=static_cast<unsigned>(abc::SendableFieldType::kGeneric);
    if(!found[i])fail(SendableError::kTypeSpelling);result.types.push_back(*found[i]);}
  std::sort(result.types.begin(),result.types.end(),[&](const auto& a,const auto& b){step();return a.field<b.field;});
  step(result.types.size()); // Final correspondence comparison is linear in fields.
  result.cost={initial.bytes-budget.bytes,initial.work-budget.work};return result;
}
}
std::variant<SendableTypeCost,SendableFailure> PrepareSendableTypes(ClassTree& tree,analysis::SemanticBudget budget){
  try{auto result=Derive(tree,budget);tree.sendable_types=std::move(result.types);return result.cost;}
  catch(const SendableFailure& e){return e;}
  catch(const std::bad_alloc&){SendableFailure e;e.kind=SendableError::kResourceLimit;return e;}
  catch(const std::length_error&){SendableFailure e;e.kind=SendableError::kResourceLimit;return e;}
}
std::variant<SendableTypeCost,SendableFailure> VerifySendableTypes(const ClassTree& tree,analysis::SemanticBudget budget){
  try{auto expected=Derive(tree,budget);if(expected.types!=tree.sendable_types){SendableFailure e;e.kind=SendableError::kCorrespondence;e.method=tree.creation.creator;e.pc=tree.creation.pc;return e;}return expected.cost;}
  catch(const SendableFailure& e){return e;}
  catch(const std::bad_alloc&){SendableFailure e;e.kind=SendableError::kResourceLimit;return e;}
  catch(const std::length_error&){SendableFailure e;e.kind=SendableError::kResourceLimit;return e;}
}
}
