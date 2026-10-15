#include "shared_module.h"
namespace abcd::source {
std::variant<SharedModuleProof,SharedModuleFailure> SharedModuleProof::Build(
    const RecordModuleContext& context,const corpus::AbcFile& file,abc::EntityOffset record,SourceContract contract) {
  auto failure=InspectSharedModule(context,file,record,contract);
  if(failure.kind!=SharedModuleError::kPreparationGate&&failure.kind!=SharedModuleError::kOutputPolicy)return failure;
  if(SharedModuleOutput(contract)!=SharedModuleOutputPolicy::kArkDirectiveV1){failure.kind=SharedModuleError::kOutputPolicy;return failure;}
  const auto* flags=context.Attributes(file,record);const auto module=context.ForOwner(file,record);
  if(!flags||!module){failure.kind=SharedModuleError::kOwner;return failure;}
  if(flags->shared!=true||flags->common_js.value_or(false)||flags->top_level_await.value_or(false)||flags->json_content){failure.kind=SharedModuleError::kAttributes;return failure;}
  // Lazy requests carry a separate dependency-order proof which is currently
  // ordinary-module only. Never make a shared record ordinary to bypass it.
  for(const auto phase:module->request_phases)if(phase!=abc::ModuleRequestPhase::kEager){failure.kind=SharedModuleError::kModuleTable;return failure;}
  return SharedModuleProof(record,module.get());
}
bool SharedModuleProof::Matches(const RecordModuleContext& context,const corpus::AbcFile& file,SourceContract contract)const {
  auto checked=Build(context,file,record_,contract);const auto* proof=std::get_if<SharedModuleProof>(&checked);
  return proof&&proof->module_==module_;
}
}
