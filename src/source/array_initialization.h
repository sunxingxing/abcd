#ifndef ABCD_SOURCE_ARRAY_INITIALIZATION_H_
#define ABCD_SOURCE_ARRAY_INITIALIZATION_H_
#include "expression_constants.h"
#include "abcd/source/source_contract.h"
namespace abcd::source {
struct EvaluationPlan;
enum class ArrayInitializationError : std::uint8_t {
  kContract, kDestination, kEscape, kCursor, kDefinition, kOrder, kCatch,
  kCorrespondence, kResourceLimit, kWorkLimit, kOperand, kSharedOperand, kCatchStaging
};
// A cursor is the end of a spread followed by a fixed number of own elements;
// absent spread means an absolute index. It is not an executable source value.
struct ArrayCursor {
  std::optional<analysis::OpId> spread;
  std::uint64_t offset = 0;
  bool operator==(const ArrayCursor&) const = default;
};
enum class ArrayStepKind : std::uint8_t { kElement, kSpread, kLength, kAdvance };
struct ArrayInitializationStep {
  analysis::OpId operation;
  ArrayStepKind kind;
  ArrayCursor before, after;
  std::optional<analysis::UseId> value;
  bool operator==(const ArrayInitializationStep&) const = default;
};
struct ArrayInitialization {
  analysis::OpId allocation;
  std::uint32_t first, count, buffered;
  bool operator==(const ArrayInitialization&) const = default;
};
struct ArrayInitializationMetadata {
  std::vector<std::optional<std::uint32_t>> buffers;
  std::vector<std::uint8_t> length_reads;
  bool operator==(const ArrayInitializationMetadata&) const = default;
};
struct ArrayInitializationPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  SourceContract contract;
  ArrayInitializationMetadata metadata;
  std::vector<ArrayInitialization> arrays;
  std::vector<ArrayInitializationStep> steps;
  // OpId -> allocation, including private length/cursor calculations.
  std::vector<std::optional<analysis::OpId>> owners;
  // Unique syntax consumers of late RHS expressions; guard peers retain their events.
  std::vector<std::optional<analysis::UseId>> nested;
  std::vector<std::optional<analysis::OpId>> guards;
  std::size_t allocated_bytes = 0, peak_bytes = 0;
  std::uint64_t work = 0;
};
// Common copy/phi identities and full-flow availability first; all-path effect
// successors then prove once-per-allocation execution across arbitrary CFG
// splits. One use census proves private identity and unobservable cursors.
// Constants/names are the adapter's original pool evidence, checked again at
// tree correspondence. Late RHS forests use the same eager expression shapes as literal operands;
// exact must-reach event successors prove each operand and commit in source order.
[[nodiscard]] std::variant<ArrayInitializationPlan, ArrayInitializationError>
BuildArrayInitializations(const analysis::ValueProgram&, SourceContract,
    ExpressionConstants, std::span<const std::u16string> names,
    analysis::SemanticBudget = {}, const EvaluationPlan* phases = nullptr);
// Conditional pool facts only: final tree correspondence must additionally
// call VerifyArrayInitializations with its independently resolved constants.
[[nodiscard]] std::variant<std::uint64_t, ArrayInitializationError>
VerifyArrayInitializationStructure(const analysis::ValueProgram&, const ArrayInitializationPlan&,
    analysis::SemanticBudget = {}, const EvaluationPlan* phases = nullptr);
// Checks supplied step equations, coverage and use census against the original
// graph, without trusting owner/cursor/private annotations.
[[nodiscard]] std::variant<std::uint64_t, ArrayInitializationError>
VerifyArrayInitializations(const analysis::ValueProgram&, const ArrayInitializationPlan&,
    SourceContract, ExpressionConstants, std::span<const std::u16string> names,
    analysis::SemanticBudget = {}, const EvaluationPlan* phases = nullptr);
}
#endif
