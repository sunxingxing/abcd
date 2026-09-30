#ifndef ABCD_ANALYSIS_CLASS_METHOD_SITES_H_
#define ABCD_ANALYSIS_CLASS_METHOD_SITES_H_

#include "abcd/abc/class_buffer.h"
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
};
enum class ClassSiteError : std::uint8_t {
  kMalformedCreation, kTargetMissing, kResourceLimit, kWorkLimit
};
struct ClassSiteFailure {
  std::variant<ClassSiteError, abc::MetadataError, abc::BodyFailure,
               abc::LiteralFailure, abc::ClassBufferError> error;
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
  [[nodiscard]] std::size_t AllocatedBytes() const { return sites_.capacity() * sizeof(ClassMethodSite); }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  ClassMethodSites() = default;
  std::vector<ClassMethodSite> sites_;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::analysis
#endif
