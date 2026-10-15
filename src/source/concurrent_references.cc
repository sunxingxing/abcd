#include "concurrent_references.h"
namespace abcd::source {
std::optional<ConcurrentFailure> ConcurrentReferenceProof::Observe(ModuleSymbol symbol) {
  if(symbol.owner.value!=input_->Record().value||symbol.symbol.value>=input_->Bindings().size()||
      consumed_>=input_->Requests().size()||input_->Bindings()[symbol.symbol.value]!=input_->Requests()[consumed_]) {
    ConcurrentFailure failure;failure.kind=ConcurrentError::kRequestUses;failure.method=input_->Method();failure.record=input_->Record();
    failure.element=static_cast<std::uint32_t>(consumed_);if(symbol.symbol.value<input_->Bindings().size())failure.request=input_->Bindings()[symbol.symbol.value].value;return failure;
  }
  ++consumed_;return {};
}
std::optional<ConcurrentFailure> ConcurrentReferenceProof::Finish() const {
  if(consumed_==input_->Requests().size())return {};
  ConcurrentFailure failure;failure.kind=ConcurrentError::kRequestUses;failure.method=input_->Method();failure.record=input_->Record();
  failure.element=static_cast<std::uint32_t>(consumed_);failure.request=input_->Requests()[consumed_].value;return failure;
}
}
