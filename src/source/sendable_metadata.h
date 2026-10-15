#ifndef ABCD_SOURCE_SENDABLE_METADATA_H_
#define ABCD_SOURCE_SENDABLE_METADATA_H_
#include "abcd/source/sendable_failure.h"
#include "abcd/abc/sendable_fields.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/source_contract.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/abc/method_lookup.h"
namespace abcd::source {
struct SendableMetadata {
  analysis::ClassCreation creation;
  abc::ClassBuffer members;
  abc::SendableFields fields;
  SendableFailure location;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Re-decode the immutable original occurrence, resolve its own regional pool,
// then follow the field literal's direct EntityId. No tree/name planning runs.
[[nodiscard]] std::variant<SendableMetadata, SendableFailure> ReadSendableMetadata(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, abc::EntityOffset record, abc::EntityOffset method,
    abc::CodeOffset, analysis::SemanticBudget = {});
// Independently repeats original-input resolution; never grants tree admission.
[[nodiscard]] std::optional<SendableFailure> VerifySendableMetadata(const SendableMetadata&,
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, abc::EntityOffset, abc::EntityOffset, abc::CodeOffset,
    analysis::SemanticBudget = {});
[[nodiscard]] SharedModuleFailure InspectSharedModule(const RecordModuleContext&,
    const corpus::AbcFile&, abc::EntityOffset, SourceContract);
}
#endif
