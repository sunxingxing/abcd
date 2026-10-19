#ifndef ABCD_SOURCE_CLASS_SELF_INITIALIZATION_H_
#define ABCD_SOURCE_CLASS_SELF_INITIALIZATION_H_
#include "abcd/source/class_self_failure.h"
#include "abcd/source/creator_class_occurrence.h"
#include "class_initializer_file.h"
namespace abcd::source {
struct FileFamily;
// These inputs are occurrence families, not method-entity summaries. A root
// retains its actual creation context; descendants retain their actual edges.
struct ClassSelfFamilyInput {
  const FileFamily& family;
  const FunctionContext& context;
};
struct ClassSelfInitializationInput {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
  const abc::MethodLookup& lookup;
  const analysis::MethodRoleIndex& roles;
  const analysis::ClassMethodSites& classes;
  const FileFunction& creator;
  CreatorClassOccurrence occurrence;
  ClassInitializerCall call;
  std::span<const ClassSelfFamilyInput>
      families;  // initializer and every class member family
};
struct ClassSelfRead {
  std::uint32_t family, member;
  analysis::OpId read, guard;
  analysis::ValueId value;
  std::optional<analysis::ValueHandler> exception;
  bool operator==(const ClassSelfRead&) const = default;
};
struct ClassSelfCapture {
  std::uint32_t family, member;
  IncomingCell incoming;
  analysis::OpId creation;
  bool self;
  bool operator==(const ClassSelfCapture& other) const {
    return family == other.family && member == other.member &&
           incoming.depth == other.incoming.depth &&
           incoming.slot.value == other.incoming.slot.value &&
           creation == other.creation && self == other.self;
  }
};
struct ClassSelfFamilyStamp {
  std::uint32_t family, member;
  abc::EntityOffset method;
  analysis::IrRevision revision;
  analysis::CfgRevision control_revision;
  bool operator==(const ClassSelfFamilyStamp&) const = default;
};
struct ClassSelfCleanup {
  analysis::HandlerId handler;
  analysis::InstructionId pop, rethrow;
  bool operator==(const ClassSelfCleanup&) const = default;
};
struct ClassSelfInitialization {
  RouteInputIdentity identity;
  CreatorClassOccurrence occurrence;
  abc::EntityOffset creator, initializer;
  analysis::IrRevision revision;
  analysis::CfgRevision control_revision;
  ClassInitializerCall call;
  analysis::OpId allocation, store;
  analysis::LexicalSlot slot;
  std::vector<ClassSelfRead> reads;
  std::vector<ClassSelfCapture> captures;
  std::vector<ClassSelfCleanup> cleanup;
  std::optional<analysis::DispatchTarget> initializer_exception;
  std::vector<ClassSelfFamilyStamp> owners;
  bool operator==(const ClassSelfInitialization& other) const {
    return identity == other.identity && occurrence == other.occurrence &&
           creator == other.creator && initializer == other.initializer &&
           revision == other.revision &&
           control_revision == other.control_revision && call == other.call &&
           allocation == other.allocation && store == other.store &&
           slot.value == other.slot.value && reads == other.reads &&
           captures == other.captures && cleanup == other.cleanup &&
           initializer_exception == other.initializer_exception &&
           owners == other.owners;
  }
};
struct ClassSelfInitializationCost {
  std::size_t bytes = 0;
  std::uint64_t work = 0;
};
// Raw initialization/capture/write evidence only. Source scope and recovery
// remain separate gates. Never interpret this as a source binding receipt.
class ClassSelfInitializationProof {
 public:
  ClassSelfInitializationProof(ClassSelfInitializationProof&&) noexcept =
      default;
  ClassSelfInitializationProof& operator=(
      ClassSelfInitializationProof&&) noexcept = default;
  static std::variant<ClassSelfInitializationProof,
                      ClassSelfInitializationFailure>
  Build(const ClassSelfInitializationInput&, analysis::SemanticBudget = {});
  const ClassSelfInitialization& Evidence() const { return evidence_; }
  const ClassSelfInitializationCost& Cost() const { return cost_; }

 private:
  ClassSelfInitializationProof() = default;
  ClassSelfInitialization evidence_;
  ClassSelfInitializationCost cost_;
};
// Fresh immutable inputs supply all storage/capture/guard and cleanup facts.
// Proposed membership and plan-local CellIds never establish correspondence.
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfInitialization(const ClassSelfInitializationInput&,
                              const ClassSelfInitialization&,
                              analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
