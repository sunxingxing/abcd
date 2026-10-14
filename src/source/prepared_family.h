#ifndef ABCD_SOURCE_PREPARED_FAMILY_H_
#define ABCD_SOURCE_PREPARED_FAMILY_H_

#include <memory>

#include "family_tree.h"
#include "family_print.h"

namespace abcd::source {
// Caller-proven destination, not facts inferred from ABC or free-name spelling.
// The enclosing generated code must already be strict. Non-simple signatures
// cannot be made strict by inserting a directive into their function body.
struct FamilyDestination {
  Strictness strictness;
  const GlobalScope* globals;
  FunctionFragment fragment = FunctionFragment::kFunctionExpression;
  // Caller-reserved syntax namespace. FamilyId remains an occurrence index.
  FunctionOwnerId first_owner{};
  // Construction only; it cannot replace a final checked printing destination.
  const ClassCaptureParent* captures = nullptr;
  // Final enclosing structure must have checked this ClassExpr and its actual
  // member roots; this borrowed view never survives synchronous printing.
  const ClassCaptureDestination* enclosure = nullptr;
};
enum class FamilyPrepareError : std::uint8_t {
  kStrictness, kCallable, kModules, kAbi, kFileChanged, kResourceLimit, kWorkLimit,
  kSources, kReceipt, kPlacement
};
struct FamilyPrepareFailure {
  std::variant<FamilyPrepareError, CreationContextFailure, FamilyFileFailure,
      FamilyTreeFailure, FamilyEnclosureFailure, ClassContextFailure> cause;
};
// Discovery/residual preparation has no enclosing AST and publishes no receipt.
// Storage identity stays in root_capture; each request keeps its own occurrence.
class FamilyPreparation {
 public:
  FamilyPreparation(FamilyPreparation&&) noexcept = default;
  FamilyPreparation& operator=(FamilyPreparation&&) noexcept = delete;
  static std::variant<FamilyPreparation, FamilyPrepareFailure> Read(
      const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
      const abc::MethodLookup&, const analysis::MethodRoleIndex&,
      const analysis::FunctionCreationSites&, FunctionContext, FamilyDestination,
      analysis::SemanticBudget, RecoveryMode, const ParentModuleInput*, const FileFunction*,
      const analysis::ClassMethodSites* = nullptr);
  const CaptureCells* RootCapture() const {
    const auto& capture = input_.Input().root_capture; return capture ? &*capture : nullptr;
  }
  const FunctionContext& Context() const { return root_; }
  std::span<const CaptureUse> RootUses() const { return input_.Input().members.front().function.Capture().Input().Captures(); }
  std::size_t MemberCount() const { return input_.Input().members.size(); }
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  friend class PreparedFamily;
  FamilyPreparation(FunctionContext root, FamilyTreeInput input, RecoveryMode recovery,
      std::size_t bytes, std::uint64_t work)
      : root_(std::move(root)), input_(std::move(input)), recovery_(recovery), bytes_(bytes), work_(work) {}
  FunctionContext root_;
  FamilyTreeInput input_;
  RecoveryMode recovery_;
  std::size_t bytes_;
  std::uint64_t work_;
};
// Immutable, file-backed preparation, not a record-completion receipt. Factory
// consumes a real creation occurrence and runs every family pass itself. No
// constructor accepts caller-built trees, diagnostic counters or cached proof.
// Caller owns/admitted file indexes. Destination spans are borrowed only during
// preparation; embedding elsewhere requires a fresh destination check.
// Explicit AllowPartial retains the existing capture-free straight-line gaps
// for class-body drafts. Such members have no ABI/source proof and keep a stage
// below TreeVerified; complete FamilyEmission rejects them before publication.
class PreparedFamily {
 public:
  PreparedFamily(PreparedFamily&&) noexcept;
  PreparedFamily& operator=(PreparedFamily&&) noexcept;
  ~PreparedFamily();
  [[nodiscard]] static std::variant<PreparedFamily, FamilyPrepareFailure> PrepareCreated(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
      analysis::FunctionSiteId site, RouteInputIdentity identity,
      FamilyDestination destination, analysis::SemanticBudget budget = {});
  [[nodiscard]] static std::variant<PreparedFamily, FamilyPrepareFailure> PrepareClass(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& functions,
      const analysis::ClassMethodSites& classes, analysis::ClassSiteId site,
      RouteInputIdentity identity, FamilyDestination destination,
      analysis::SemanticBudget budget = {}, RecoveryMode recovery = RecoveryMode::kCompleteOnly,
      const ParentModuleInput* modules = nullptr, const FileFunction* creator = nullptr);
  [[nodiscard]] static std::variant<PreparedFamily, FamilyPrepareFailure> PrepareConstructor(
      const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
      const abc::MethodLookup&, const analysis::MethodRoleIndex&,
      const analysis::FunctionCreationSites&, const FileFunction& creator,
      analysis::OpId creation, RouteInputIdentity, FamilyDestination,
      analysis::SemanticBudget budget = {}, RecoveryMode recovery = RecoveryMode::kCompleteOnly);
  [[nodiscard]] static std::variant<PreparedFamily, FamilyPrepareFailure> PrepareDefinedMember(
      const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
      const abc::MethodLookup&, const analysis::MethodRoleIndex&,
      const analysis::FunctionCreationSites&, const FileFunction&, analysis::OpId,
      abc::ClassMethodKind, RouteInputIdentity, FamilyDestination, analysis::SemanticBudget = {},
      RecoveryMode recovery = RecoveryMode::kCompleteOnly);
  // Preparation storage is already admitted. Returned cost covers this stage only.
  static std::variant<PreparedFamily, FamilyPrepareFailure> Finish(
      const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
      const abc::MethodLookup&, FamilyPreparation, FamilyDestination,
      analysis::SemanticBudget, const FileFunction*);
  [[nodiscard]] const ConstructedFamily& Family() const;
  [[nodiscard]] const RouteInputIdentity& Identity() const;
  [[nodiscard]] const analysis::ClassMethodSite* ClassCreation() const;
  [[nodiscard]] const ClassDefinedMember* DefinedMemberCreation() const;
  [[nodiscard]] const analysis::FunctionCreationSite* FunctionCreation() const;
  [[nodiscard]] std::u16string_view Name() const;
  // Occurrence/member order, not deduplicated by method entity. TreeVerified
  // remains Incomplete until the owning emission succeeds in its destination.
  [[nodiscard]] std::span<const MethodRouteReceipt> Receipts() const;
  // Conservative preparation charge, including released scratch, not RSS.
  [[nodiscard]] std::size_t AllocatedBytes() const;
  [[nodiscard]] std::uint64_t Work() const;
  [[nodiscard]] std::variant<FamilyEnclosure, FamilyPrepareFailure> CheckDestination(
      FamilyDestination destination, analysis::SemanticBudget budget = {}) const;
  // Shared printer and original-PC positions, still no executable receipt.
  // Rechecks the actual embedding before writing any bytes. On output failure
  // the caller must discard the partial stream, as with PrintFamilyDraft.
  // Optional preparation is borrowed only during synchronous printing; its
  // exact trees and external context must stay unchanged throughout the call.
  [[nodiscard]] std::variant<PrintedFamilyDraft, FamilyPrepareFailure, TreePrintFailure> PrintDraft(
      FamilyDestination destination, std::ostream& output,
      EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {}, std::size_t indent = 0,
      const ClassNames* prepared = nullptr) const;
 private:
  struct Storage;
  static std::variant<PreparedFamily, FamilyPrepareFailure> Prepare(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
      FunctionContext context, FamilyDestination destination, analysis::SemanticBudget budget,
      RecoveryMode recovery = RecoveryMode::kCompleteOnly, const ParentModuleInput* modules = nullptr,
      const FileFunction* creator = nullptr);
  explicit PreparedFamily(std::unique_ptr<const Storage> storage);
  std::unique_ptr<const Storage> storage_;
  std::size_t preparation_bytes_ = 0;
  std::uint64_t preparation_work_ = 0;
};
}  // namespace abcd::source
#endif
