#ifndef ABCD_SOURCE_CLASS_TREE_CONTEXT_H_
#define ABCD_SOURCE_CLASS_TREE_CONTEXT_H_

#include "abcd/abc/call_type.h"
#include "abcd/analysis/class_method_sites.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
struct FileFunction;
enum class ClassContextError : std::uint8_t {
  kSite, kProfile, kRole, kKind, kName, kSignature, kResourceLimit, kWorkLimit
};
struct ClassConstructorSite {
  abc::EntityOffset target;
  abc::EntityOffset creator;
  abc::CodeOffset pc;
  analysis::ClassCreation creation;
};
struct ClassContextFailure {
  std::variant<ClassContextError, abc::MetadataError,
               abc::MethodAttributeError, abc::FrameError> error;
};
// A selected, actual creation occurrence. It does not claim that every use of
// the same method has this owner, or that the containing class/record is ready.
// Same-file identity/index/roles/sites are caller-owned immutable input facts.
class ClassTreeContext {
 public:
  ClassTreeContext(ClassTreeContext&&) noexcept = default;
  ClassTreeContext& operator=(ClassTreeContext&&) noexcept = default;
  [[nodiscard]] static std::variant<ClassTreeContext, ClassContextFailure> Resolve(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::ClassMethodSites& sites,
      analysis::ClassSiteId site, RouteInputIdentity identity,
      analysis::SemanticBudget budget = {});
  // Fresh unchanged ReadFunctionValues caller and same file indexes. The class
  // operation must retain its original occurrence and definite no-extends input.
  [[nodiscard]] static std::variant<ClassTreeContext, ClassContextFailure> ResolveConstructor(
      const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
      const abc::MethodLookup&, const analysis::MethodRoleIndex&,
      const FileFunction& creator, analysis::OpId creation, RouteInputIdentity,
      analysis::SemanticBudget budget = {});
  const ClassConstructorSite* Constructor() const { return std::get_if<ClassConstructorSite>(&site_); }
  abc::EntityOffset Target() const { return std::visit([](const auto& site) { return site.target; }, site_); }
  std::uint32_t Length() const { return Constructor() ? Constructor()->creation.length : Site().length; }
  [[nodiscard]] const RouteInputIdentity& Identity() const { return identity_; }
  [[nodiscard]] const analysis::ClassMethodSite& Site() const { return std::get<analysis::ClassMethodSite>(site_); }
  [[nodiscard]] abc::MethodPosition Position() const { return position_; }
  [[nodiscard]] const abc::FrameLayout& Frame() const { return frame_; }
  [[nodiscard]] std::u16string_view Name() const { return name_; }
  [[nodiscard]] CallableKind Callable() const { return callable_; }
  [[nodiscard]] Strictness Mode() const { return Strictness::kStrict; }
  [[nodiscard]] std::size_t AllocatedBytes() const { return sizeof(*this) + (name_.capacity() + 1) * sizeof(char16_t); }
  [[nodiscard]] std::uint64_t Work() const { return work_; }
 private:
  ClassTreeContext() = default;
  RouteInputIdentity identity_;
  std::variant<analysis::ClassMethodSite, ClassConstructorSite> site_;
  abc::MethodPosition position_;
  abc::FrameLayout frame_;
  CallableKind callable_ = CallableKind::kMethod;
  std::u16string name_;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
