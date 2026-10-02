#include "module_capture_prefix.h"
#include "module_binding_name.h"
#include "abcd/corpus/abc_file.h"
#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<std::optional<ModuleCapturePrefix>, ModulePrefixError> ModuleCapturePrefix::Build(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const abc::MethodInfo& method, const RecordModuleContext& context,
    const FunctionInput& input, analysis::SemanticBudget budget) {
  using Error = ModulePrefixError;
  try {
    const auto none = [] { return std::optional<ModuleCapturePrefix>{}; };
    if (method.offset != input.Method() || method.owner_offset != input.Record() || method.name != "func_main_0" ||
        !context.OrdinarySynchronous(file,input.Record()) ||
        ResourceFailures(input.Identity().contract) != ResourceFailureScope::kExcluded) return none();
    const auto attributes = abc::DecodeMethodAttributes(method.access_flags, header.profile);
    const auto* attr = std::get_if<abc::MethodAttributes>(&attributes);
    if (!attr || attr->sendable || (attr->kind != abc::FunctionKind::kNone && attr->kind != abc::FunctionKind::kBaseConstructor)) return none();
    const auto initial_work=budget.work;
    const auto& candidates = input.Proof().locals.module_prefix_initializations;
    if (candidates.size()>budget.work) return Error::kWorkLimit;
    budget.work-=candidates.size();
    if (std::none_of(candidates.begin(),candidates.end(),[](const auto& x){return x.has_value();})) return none();
    if (file.CheckUnchanged()) return Error::kChanged;
    const auto module=context.ForOwner(file,input.Record()); if (!module) return none();
    auto named=ResolveModuleBindingNames(file,header,*module,{budget.bytes/2,budget.work});
    const auto* slots=std::get_if<ModuleBindingNames>(&named);
    if (!slots) {
      const auto& failure=std::get<ModuleBindingNameFailure>(named);
      if (const auto* error=std::get_if<abc::MetadataError>(&failure))
        return *error==abc::MetadataError::kFileChanged ? Error::kChanged :
            *error==abc::MetadataError::kResourceLimit ? Error::kResourceLimit : Error::kRead;
      if (std::get<ModuleBindingNameError>(failure)==ModuleBindingNameError::kName) return none();
      return std::get<ModuleBindingNameError>(failure)==ModuleBindingNameError::kWorkLimit ? Error::kWorkLimit :
          std::get<ModuleBindingNameError>(failure)==ModuleBindingNameError::kResourceLimit ? Error::kResourceLimit : Error::kRead;
    }
    if (slots->allocated_bytes>budget.bytes || slots->work>budget.work) return Error::kResourceLimit;
    budget.bytes-=slots->allocated_bytes;
    budget.work-=slots->work;
    // Root proof is valid only for the unchanged input. A prefix count makes
    // qualification O(log metadata + instructions + cells), not per-cell scans.
    const auto& instructions=input.Input().instructions;
    const auto width=sizeof(std::size_t);
    if (instructions.size()==SIZE_MAX || instructions.size()+1>budget.bytes/width) return Error::kResourceLimit;
    if (instructions.size()+1>budget.work) return Error::kWorkLimit;
    budget.work-=instructions.size()+1;
    std::vector<std::size_t> invalid(instructions.size()+1);
    const auto used=invalid.capacity()*width;
    if (used>budget.bytes) return Error::kResourceLimit;
    auto bytes=budget.bytes-used;
    auto work=budget.work;
    const auto step=[&](std::size_t n=1){if(n>work)throw Error::kWorkLimit;work-=n;};
    for (std::size_t i=0;i<instructions.size();++i) {
      step(); bool bad=true;
      const auto& operation=instructions[i].operation;
      if (std::holds_alternative<analysis::LexicalStore>(operation) ||
          (std::holds_alternative<analysis::ScalarAssignment>(operation) &&
           instructions[i].effects.normal == analysis::LanguageEffect::kNone)) bad=false;
      if (const auto* store=std::get_if<analysis::ModuleStore>(&operation)) bad=store->slot.value>=slots->locals.size();
      if (const auto* creation=std::get_if<analysis::FunctionCreation>(&operation)) {
        step(std::bit_width(index.regions.size())+std::bit_width(index.methods.size())+1);
        const auto target=abc::ResolveMethodTarget(file,header,index,lookup,method.offset,creation->method);
        if (const auto* error=std::get_if<abc::MetadataError>(&target))
          return *error==abc::MetadataError::kFileChanged ? Error::kChanged :
              *error==abc::MetadataError::kResourceLimit ? Error::kResourceLimit : Error::kRead;
        const auto* resolved=std::get_if<abc::MethodTarget>(&target);
        bad=creation->kind!=analysis::CreationKind::kFunction || !resolved || resolved->attributes.sendable ||
            resolved->attributes.kind==abc::FunctionKind::kConcurrent ||
            (resolved && index.methods[resolved->position.value].owner_offset!=input.Record());
      }
      invalid[i+1]=invalid[i]+bad;
    }
    ModuleCapturePrefix result;
    using Prefix=decltype(result.prefixes_)::value_type;
    if (candidates.size()>bytes/sizeof(Prefix)) return Error::kResourceLimit;
    step(candidates.size());
    result.prefixes_.resize(candidates.size());
    if (result.prefixes_.capacity()>bytes/sizeof(Prefix)) return Error::kResourceLimit;

    for (std::size_t i=0;i<candidates.size();++i) {
      step(); if (!candidates[i]) continue;
      const auto& prefix=*candidates[i];
      if (prefix.allocation.value>=prefix.store.value || prefix.store.value>=instructions.size()) return Error::kRead;
      if (!std::holds_alternative<analysis::PushEnvironment>(instructions[prefix.allocation.value].operation) ||
          !std::holds_alternative<analysis::LexicalStore>(instructions[prefix.store.value].operation)) continue;
      step(std::bit_width(input.Input().flow.blocks.size())+std::bit_width(input.Proof().locals.stores.size())+1);
      const auto& blocks=input.Input().flow.blocks;
      auto block=std::upper_bound(blocks.begin(),blocks.end(),prefix.allocation.value,
          [](auto pc,const auto& b){return pc<b.first_instruction;});
      if (block==blocks.begin()) continue;
      --block;
      if (prefix.store.value-block->first_instruction>=block->instruction_count) continue;
      const auto& stores=input.Proof().locals.stores;
      const auto key=std::pair{static_cast<std::uint32_t>(i),prefix.store.value};
      auto stored=std::lower_bound(stores.begin(),stores.end(),key,
          [](const auto& x,auto k){return std::pair{x.cell.value,x.instruction.value}<k;});
      if (stored==stores.end() || std::pair{stored->cell.value,stored->instruction.value}!=key) continue;
      if (invalid[prefix.store.value+1]!=invalid[prefix.allocation.value+1]) continue;
      result.prefixes_[i]=prefix;
    }

    if (file.CheckUnchanged()) return Error::kChanged;
    result.work_=initial_work-work;
    result.identity_=input.Identity(); result.method_=input.Method();
    result.instructions_=instructions.data(); result.cells_=input.Proof().locals.cells.data();
    return std::optional<ModuleCapturePrefix>{std::move(result)};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
