#ifndef ABCD_SOURCE_CREATOR_CLASSES_H_
#define ABCD_SOURCE_CREATOR_CLASSES_H_
#include "class_tree.h"

namespace abcd::source {
// Borrowed input identity belongs to this occurrence, not to the constructor's
// method entity. No source symbol or class body is allocated by discovery.
struct CreatorClassOccurrence {
  abc::EntityOffset family_root{};
  FunctionOwnerId first_owner{};
  FamilyOccurrenceId member{};
  bool operator==(const CreatorClassOccurrence&) const = default;
};
struct CreatorClassInput {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
  const abc::MethodLookup& lookup;
  const analysis::MethodRoleIndex& roles;
  const analysis::FunctionCreationSites& functions;
  const analysis::ClassMethodSites& classes;
  const FileFunction& creator;
  RouteInputIdentity identity;
  CreatorClassOccurrence occurrence;
};
struct ClassOwnerRange {
  std::uint64_t begin, end;  // Half-open; end may be UINT32_MAX + 1.
  bool operator==(const ClassOwnerRange&) const = default;
};
struct CreatorClassTask {
  analysis::OpId operation;
  std::shared_ptr<ClassTree> tree;
  std::span<const ClassDefinitionSite> definitions;
  const ClassInitializerCall* initializer = nullptr;
  std::optional<ClassPreparation> preparation;
  // Absent until all ordinary member-family occurrences have been counted.
  // A discovered layout never pretends to reserve a complete owner namespace.
  std::optional<ClassOwnerRange> owners;
};
struct CreatorClasses {
  CreatorClassOccurrence occurrence;
  RouteInputIdentity identity;
  abc::EntityOffset creator{};
  analysis::IrRevision value_revision{};
  analysis::CfgRevision cfg_revision{};
  std::vector<ClassDefinitionSite> definitions;
  std::vector<CreatorClassTask> tasks;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
struct CreatorClassFailure {
  ClassTreeFailure cause;
  std::optional<analysis::OpId> operation;
};
struct CreatorClassCost { std::size_t allocated_bytes; std::uint64_t work; };
[[nodiscard]] std::variant<CreatorClasses, CreatorClassFailure> DiscoverCreatorClasses(
    const CreatorClassInput&, analysis::SemanticBudget = {});
// Re-resolves original pool identities and checks complete occurrence coverage.
// Discovery is not capture/declaration/barrier fulfilment or printing permission.
[[nodiscard]] std::variant<CreatorClassCost, CreatorClassFailure> VerifyCreatorClasses(
    const CreatorClassInput&, const CreatorClasses&, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<CreatorClassCost, CreatorClassFailure> PrepareCreatorClasses(
    const CreatorClassInput&, CreatorClasses&, const GlobalScope*, FunctionOwnerId,
    RecoveryMode, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<CreatorClassCost, CreatorClassFailure> FinishCreatorClasses(
    const CreatorClassInput&, CreatorClasses&, const GlobalScope*, FunctionOwnerId,
    const CaptureTree*, RecoveryMode, analysis::SemanticBudget = {});
[[nodiscard]] std::variant<CreatorClassCost, CreatorClassFailure> VerifyCreatorClassOwners(
    const CreatorClasses&, FunctionOwnerId, analysis::SemanticBudget = {});
// Allocation-free validation used at preparation and final construction.
[[nodiscard]] bool ValidClassOwnerRange(ClassOwnerRange range, std::uint64_t expected);
}
#endif
