#ifndef ABCD_SOURCE_SEMANTIC_SOURCE_H_
#define ABCD_SOURCE_SEMANTIC_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "abcd/abc/frame_layout.h"
#include "abcd/analysis/semantic_region.h"
#include "abcd/analysis/semantic_validation.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
struct MemberExpression {
  analysis::ScalarValue object;
  abc::PoolIndex name;
};
struct StringExpression {
  abc::PoolIndex value;
};
struct CallExpression {
  analysis::OrdinaryCall call;
};
struct MemberCallExpression {
  abc::PoolIndex name;
  analysis::ReceiverCall call;
};
struct ModuleExpression {
  analysis::ModuleLoad binding;
};
struct GlobalExpression {
  abc::PoolIndex name;
};
struct CapturedExpression {
  analysis::CapturedSlot cell;
};
struct LocalExpression { analysis::LocalCellId cell; };
struct FunctionExpression {
  analysis::FunctionCreation creation;
  analysis::InstructionId instruction;
  std::optional<analysis::InstructionId> previous_creation;
};
using SourceExpression =
    std::variant<analysis::ScalarExpression, MemberExpression,
                 StringExpression, CallExpression, MemberCallExpression,
                 ModuleExpression, analysis::EmptyObjectCreation,
                 analysis::BufferedObjectCreation, GlobalExpression,
                 analysis::ConstructorCall, CapturedExpression,
                 FunctionExpression, LocalExpression, analysis::RestCopy,
                 analysis::BufferedArrayCreation>;
struct SourceAssignment {
  analysis::ScalarSlot target;
  SourceExpression value;
};
struct SourceMemberStore {
  analysis::ScalarValue object;
  abc::PoolIndex name;
  analysis::ScalarValue value;
};
struct SourceLocalStore {
  analysis::LocalCellId cell;
  analysis::ScalarSlot value;
};
struct SourceOwnStore {
  analysis::OwnWrite write;
  analysis::BufferedObjectCreation origin;
};
struct SourceConditionalBreak {
  analysis::UnaryExpression condition;
  analysis::BlockId target;
};
struct SourceBreak {
  analysis::BlockId target;
};
struct SourceReturn {
  analysis::ScalarValue value;
};
struct SourceStagedCall {
  abc::PoolIndex name;
  analysis::ReceiverCall call;
  analysis::InstructionId first_argument_statement;
  analysis::InstructionId call_statement;
};
using SourceOperation =
    std::variant<analysis::NoOperation, SourceAssignment, SourceMemberStore, SourceOwnStore,
                 SourceLocalStore,
                 SourceConditionalBreak, SourceBreak, SourceReturn,
                 SourceStagedCall>;
struct SourceStatement {
  abc::CodeOffset pc;
  SourceOperation operation;
  // A fused property read and optional dead spill/reload share the call span.
  // Their PCs are retained, never recovered by searching generated text.
  std::optional<abc::CodeOffset> fused_read;
  std::optional<abc::CodeOffset> fused_spill;
  std::optional<abc::CodeOffset> fused_reload;
  std::size_t read_distance = 0;
  std::size_t spill_distance = 0;
  std::size_t reload_distance = 0;
  std::optional<abc::CodeOffset> previous_origin;
};
struct SourceArgumentRegion {
  analysis::InstructionId property;
  analysis::InstructionId call;
  analysis::BlockId first_block;
  analysis::BlockId last_block;
};
struct SourceAst {
  analysis::SemanticRegionTree regions;
  std::vector<SourceStatement> statements;
  std::vector<std::optional<analysis::BlockId>> fallthrough;
  std::vector<analysis::LocalCell> local_cells;
  std::optional<SourceArgumentRegion> argument_region;
  std::size_t allocated_bytes = 0;
};
enum class SourceError : std::uint8_t {
  kUnsupportedOperation,
  kMissingTarget,
  kInvalidEffect,
  kResourceLimit,
  kConstant,
  kOutput,
  kCreation,
  kSuperInitialization,
};
struct SourceFailure {
  SourceError kind;
  abc::CodeOffset pc;
};
using SourceAstResult = std::variant<SourceAst, SourceFailure>;

// Source nodes retain exactly one PC from semantic IR; region nodes encode
// control structure without copying effectful instructions. Unsupported
// effects fail before any text is published.
// named_scopes is the unchanged same-body ValidateNamedScopes certificate.
// rest is the unchanged same-body/frame ProveRestPrologue certificate; the
// callable owner must emit its matching native rest formal before this body.
[[nodiscard]] SourceAstResult BuildSourceAst(
    const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof,
    analysis::SemanticRegionTree regions, std::size_t budget_bytes,
    std::span<const std::uint8_t> named_scopes = {},
    std::optional<analysis::RestPrologue> rest = {});

struct GeneratedPosition {
  std::size_t byte = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 0;
  // Template emission cursor: distinguishes before/after a zero-width
  // capture at this byte. Always zero in fully bound source coordinates.
  std::size_t captures = 0;
};
struct GeneratedSpan {
  abc::CodeOffset pc;
  GeneratedPosition begin;
  GeneratedPosition end;
};
struct PrintedSource {
  std::string text;
  std::vector<GeneratedSpan> spans;
};
struct SourceCapture {
  GeneratedPosition position;
  analysis::CapturedSlot cell;
};
// Not executable source. Positions are template coordinates, prior to capture
// insertion; never expose them as final generated-file positions.
struct PrintedTemplate {
  std::string text;
  std::vector<GeneratedSpan> spans;
  std::vector<SourceCapture> captures;
};
// source is the unchanged PrintSourceTemplate result. bindings is one
// parent-PROVEN initialized local cell per capture occurrence,
// in emission order, rebased at this exact creation site. The owning scope
// declares the private $lex<environment>_<slot> bindings without collisions.
// This substitutes bindings, not initialization/capture proof. No source text
// is parsed to recover positions. O(text + C + S log C), bounded scratch.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> BindSourceTemplate(
    const PrintedTemplate& source,
    std::span<const analysis::LocalCell> bindings,
    std::size_t budget_bytes);
enum class SourceForm : std::uint8_t { kFunction, kClassConstructor };

// The printer records each span as it emits its AST node, including zero-width
// anchors for elided NOPs. No source-text search or sourceMap input is used.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> PrintSourceAst(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, std::size_t output_bytes,
    const analysis::SemanticProof* proof = nullptr);

// Caller supplies distinct private register bindings in an already validated
// strict callable. This emits only its body; spans are relative to that body.
// Optional proof belongs to this unchanged method/AST and remains live during
// printing. It enables binding child captures to proven initialized locals;
// absence never authorizes guessed captures. Child-internal spans are separate.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> PrintSourceBody(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes,
    const analysis::SemanticProof* proof = nullptr);
// Captured expressions become typed insertions, not invented identifiers.
// Only a parent capture-binding path may consume this incomplete artifact.
[[nodiscard]] std::variant<PrintedTemplate, SourceFailure> PrintSourceTemplate(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes,
    const analysis::SemanticProof* proof = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_SOURCE_H_
