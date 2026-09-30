#ifndef ABCD_SOURCE_CREATION_TREE_CONTEXT_H_
#define ABCD_SOURCE_CREATION_TREE_CONTEXT_H_

#include "class_tree_context.h"
#include "abcd/analysis/function_creation_sites.h"

namespace abcd::source {
enum class CreationContextError : std::uint8_t {
  kSite, kProfile, kRole, kKind, kName, kSignature, kResourceLimit, kWorkLimit
};
struct CreationContextFailure {
  std::variant<CreationContextError, abc::MetadataError, abc::FrameError> error;
};
enum class CreationContextMode : std::uint8_t { kFunction, kMethodBody };
// Actual DEFINEFUNC occurrence, not a class-buffer member or a proof that
// captures/home-object obligations are closed. Factory body verification
// remains mandatory; no arbitrary caller-supplied tree can acquire this owner.
class CreationTreeContext {
 public:
  CreationTreeContext(CreationTreeContext&&) noexcept = default;
  CreationTreeContext& operator=(CreationTreeContext&&) noexcept = default;
  [[nodiscard]] static std::variant<CreationTreeContext, CreationContextFailure> Resolve(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
      analysis::FunctionSiteId site, RouteInputIdentity identity, analysis::SemanticBudget budget = {},
      CreationContextMode mode = CreationContextMode::kFunction);
  const RouteInputIdentity& Identity() const { return identity_; }
  const analysis::FunctionCreationSite& Site() const { return site_; }
  abc::MethodPosition Position() const { return site_.resolved.position; }
  const abc::FrameLayout& Frame() const { return frame_; }
  std::u16string_view Name() const { return name_; }
  CallableKind Callable() const { return callable_; }
  Strictness Mode() const { return Strictness::kStrict; }
  std::size_t AllocatedBytes() const { return sizeof(*this) + (name_.capacity() + 1) * sizeof(char16_t); }
  std::uint64_t Work() const { return work_; }
 private:
  CreationTreeContext() = default;
  RouteInputIdentity identity_;
  analysis::FunctionCreationSite site_{};
  abc::FrameLayout frame_;
  std::u16string name_;
  CallableKind callable_ = CallableKind::kFunction;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
