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
};
enum class FamilyPrepareError : std::uint8_t {
  kStrictness, kCallable, kModules, kAbi, kFileChanged, kResourceLimit, kWorkLimit,
  kSources, kReceipt, kPlacement
};
struct FamilyPrepareFailure {
  std::variant<FamilyPrepareError, CreationContextFailure, FamilyFileFailure,
      FamilyTreeFailure, FamilyEnclosureFailure, ClassContextFailure> cause;
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
      const ParentModuleInput* modules = nullptr);
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
  [[nodiscard]] std::variant<PrintedFamilyDraft, FamilyPrepareFailure, TreePrintError> PrintDraft(
      FamilyDestination destination, std::ostream& output,
      EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {}, std::size_t indent = 0) const;
 private:
  struct Storage;
  static std::variant<PreparedFamily, FamilyPrepareFailure> Prepare(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
      FunctionContext context, FamilyDestination destination, analysis::SemanticBudget budget,
      RecoveryMode recovery = RecoveryMode::kCompleteOnly, const ParentModuleInput* modules = nullptr);
  explicit PreparedFamily(std::unique_ptr<const Storage> storage);
  std::unique_ptr<const Storage> storage_;
};
}  // namespace abcd::source
#endif
