#include "concurrent_context.h"

#include <algorithm>
#include <bit>
namespace abcd::source {
std::variant<ConcurrentMetadata,ConcurrentFailure> ReadConcurrentCreation(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodInfo& method,
    const analysis::FunctionCreationSite& site,
    analysis::FunctionSiteId occurrence,
    analysis::SemanticBudget budget) {
  const auto initial_work=budget.work;
  ConcurrentFailure failure;
  failure.method = method.offset;
  failure.record = method.owner_offset;
  failure.creator = site.creator;
  failure.creation_pc = site.pc;
  failure.occurrence = occurrence;
  const auto step = [&](std::uint64_t n) {
    if (n > budget.work) return false;
    budget.work -= n;
    return true;
  };
  if (!step(std::bit_width(index.records.size()) + 1)) {
    failure.kind = ConcurrentError::kWorkLimit;
    return failure;
  }
  // BuildMetadataIndex emits records in sorted class-offset order.
  const auto record = std::lower_bound(
      index.records.begin(), index.records.end(), method.owner_offset,
      [](const auto& r, auto owner) { return r.offset < owner; });
  if (record == index.records.end() || record->offset != method.owner_offset) {
    failure.kind = ConcurrentError::kOwner;
    return failure;
  }
  // Bound the field and module decoding before their existing bounded readers.
  // No per-method scan of the file or of other records.
  // ReadStringEntity validates raw bytes and converts UTF-16; geometric raw
  // growth copies fewer than twice the raw bytes. Charge these four linear
  // passes against the file-size upper bound for EVERY field, including aliases
  // and unrelated names, before entering the reader. Include the possible
  // field-type region lookup. Never rely on a later module read to pay for
  // names.
  constexpr std::uint64_t kNamePasses = 4;
  const std::uint64_t field_work =
      kNamePasses * header.file_size + std::bit_width(index.regions.size()) + 1;
  if (record->field_count > budget.work / field_work ||
      !step(field_work * record->field_count)) {
    failure.kind = ConcurrentError::kWorkLimit;
    return failure;
  }
  const auto parsed = abc::ReadMergedRecordAttributes(file, header, index,
                                                      *record, {budget.bytes});
  if (const auto* e = std::get_if<abc::AttributeFailure>(&parsed)) {
    failure.kind = e->metadata_error == abc::MetadataError::kResourceLimit
                       ? ConcurrentError::kResourceLimit
                       : ConcurrentError::kModule;
    failure.cause = e->kind;
    failure.metadata = e->metadata_error;
    return failure;
  }
  const auto& attrs = std::get<abc::RecordAttributes>(parsed);
  if (!attrs.module || attrs.common_js.value_or(false)) {
    failure.kind = ConcurrentError::kModule;
    return failure;
  }
  if (!step(header.file_size)) {
    failure.kind = ConcurrentError::kWorkLimit;
    return failure;
  }
  auto decoded = abc::ReadModuleInfo(
      file, header, *attrs.module, {budget.bytes}, attrs.request_phases);
  if (const auto* e = std::get_if<abc::ModuleFailure>(&decoded)) {
    failure.kind = e->kind == abc::ModuleError::kResourceLimit
                       ? ConcurrentError::kResourceLimit
                       : ConcurrentError::kModule;
    failure.cause = e->kind;
    return failure;
  }
  const auto& module = std::get<abc::ModuleInfo>(decoded);
  const auto retained =
      module.requests.capacity() * sizeof(abc::EntityOffset) +
      module.imports.capacity() * sizeof(abc::ModuleImport) +
      module.namespace_imports.capacity() * sizeof(abc::NamespaceImport) +
      module.local_exports.capacity() * sizeof(abc::LocalExport) +
      module.indirect_exports.capacity() * sizeof(abc::IndirectExport) +
      module.star_exports.capacity() * sizeof(abc::ModuleRequestId) +
      module.request_phases.capacity() * sizeof(abc::ModuleRequestPhase);
  if (retained > budget.bytes) {
    failure.kind = ConcurrentError::kResourceLimit;
    return failure;
  }
  auto requests = abc::ReadConcurrentRequests(
      file, header, index, method, record->offset, module.requests,
      {budget.bytes - retained, budget.work});
  if (const auto* e = std::get_if<abc::ConcurrentRequestFailure>(&requests)) {
    failure.kind = e->kind == abc::ConcurrentRequestError::kResourceLimit
                       ? ConcurrentError::kResourceLimit
                   : e->kind == abc::ConcurrentRequestError::kWorkLimit
                       ? ConcurrentError::kWorkLimit
                       : ConcurrentError::kRequests;
    failure.cause = e->kind;
    failure.metadata = e->metadata;
    failure.annotation = e->annotation;
    failure.element = e->element;
    failure.request = e->request;
    return failure;
  }
  const auto& evidence = std::get<abc::ConcurrentRequests>(requests);
  if (evidence.entries.size() > UINT32_MAX) {
    failure.kind = ConcurrentError::kResourceLimit;
    return failure;
  }
  failure.annotation = evidence.first_annotation;
  failure.annotation_present = evidence.present;
  failure.request_count = static_cast<std::uint32_t>(evidence.entries.size());
  ConcurrentMetadata result;
  result.allocated_bytes=retained+evidence.allocated_bytes;
  result.work=initial_work-budget.work+evidence.work;
  result.location=failure;
  result.module=std::get<abc::ModuleInfo>(std::move(decoded));
  result.requests=std::get<abc::ConcurrentRequests>(std::move(requests));
  return result;
}
ConcurrentFailure InspectConcurrentCreation(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index,const abc::MethodInfo& method,
    const analysis::FunctionCreationSite& site,analysis::FunctionSiteId occurrence,
    SourceContract contract,analysis::SemanticBudget budget) {
  auto read=ReadConcurrentCreation(file,header,index,method,site,occurrence,budget);
  if(const auto* failure=std::get_if<ConcurrentFailure>(&read))return *failure;
  auto failure=std::get<ConcurrentMetadata>(read).location;
  failure.kind=ConcurrentOutput(contract)==ConcurrentOutputPolicy::kArkDirectiveV1?
      ConcurrentError::kPreparationGate:ConcurrentError::kOutputPolicy;
  return failure;
}
}  // namespace abcd::source
