#ifndef ABCD_ANALYSIS_CLASS_METHOD_SITES_H_
#define ABCD_ANALYSIS_CLASS_METHOD_SITES_H_

#include <algorithm>
#include "abcd/abc/class_buffer.h"
#include "abcd/abc/sendable_fields.h"
#include "abcd/abc/method_lookup.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
struct ClassSiteId { std::uint32_t value = 0; auto operator<=>(const ClassSiteId&) const = default; };
struct ClassSiteRange { ClassSiteId first; std::uint32_t count = 0; };
struct ClassMethodSite {
  abc::EntityOffset target;
  abc::EntityOffset creator;
  abc::CodeOffset pc;
  abc::EntityOffset literal;
  std::uint32_t property;
  abc::LiteralEntry key;
  abc::ClassMethodKind kind;
  abc::ClassPlacement placement;
  std::uint16_t length;
  ClassCreation creation;
};
enum class ClassSiteError : std::uint8_t {
  kMalformedCreation, kTargetMissing, kResourceLimit, kWorkLimit
};
struct SendableFieldName { abc::EntityOffset key; std::u16string text; };
struct IndexedSendableLiteral {
  abc::EntityOffset literal;
  abc::ClassBuffer members;
  abc::SendableFields fields;
  std::vector<SendableFieldName> field_names; // Sorted unique string entities.
  // A malformed field description refuses its class, not unrelated records.
  // Resource failures still belong to the file-index budget/process owner.
  std::optional<abc::SendableFieldFailure> field_failure;
  std::optional<std::uint32_t> field_failure_mask;
  [[nodiscard]] const std::u16string* FieldName(std::size_t ordinal) const {
    if(ordinal>=fields.fields.size())return nullptr;
    const auto* key=std::get_if<abc::EntityOffset>(&fields.fields[ordinal].key.value);if(!key)return nullptr;
    const auto at=std::lower_bound(field_names.begin(),field_names.end(),*key,
        [](const auto& name,auto id){return name.key<id;});
    return at!=field_names.end() && at->key==*key ? &at->text : nullptr;
  }
};
struct ClassSiteFailure {
  std::variant<ClassSiteError, abc::MetadataError, abc::BodyFailure,
               abc::LiteralFailure, abc::ClassBufferError, abc::SendableFieldFailure> error;
};
// Immutable file-level occurrence index, not callable-kind or capture proof.
// Keeps every ordinary DEFINECLASSWITHBUFFER occurrence; the same method may
// have multiple owners. Sendable/legacy sites are not mislabeled as ordinary.
// Combine with MethodRoleIndex and target attributes at source admission.
class ClassMethodSites {
 public:
  ClassMethodSites(ClassMethodSites&&) noexcept = default;
  ClassMethodSites& operator=(ClassMethodSites&&) noexcept = default;
  [[nodiscard]] static std::variant<ClassMethodSites, ClassSiteFailure> Build(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      SemanticBudget budget = {});
  [[nodiscard]] ClassSiteRange Find(abc::EntityOffset method) const;
  [[nodiscard]] std::optional<ClassSiteId> FindOccurrence(abc::EntityOffset target,
      abc::EntityOffset creator, abc::CodeOffset pc, std::uint32_t property) const;
  [[nodiscard]] const ClassMethodSite* At(ClassSiteId site) const {
    return site.value < sites_.size() ? &sites_[site.value] : nullptr;
  }
  [[nodiscard]] std::shared_ptr<const IndexedSendableLiteral> SendableLiteral(abc::EntityOffset literal) const;
  [[nodiscard]] std::size_t AllocatedBytes() const { return allocated_bytes_; }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  ClassMethodSites() = default;
  std::vector<ClassMethodSite> sites_;
  std::vector<std::shared_ptr<const IndexedSendableLiteral>> sendable_;
  std::size_t allocated_bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::analysis
#endif
