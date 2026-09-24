#ifndef ABCD_SOURCE_SEMANTIC_SOURCE_H_
#define ABCD_SOURCE_SEMANTIC_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
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
using SourceExpression =
    std::variant<analysis::ScalarExpression, MemberExpression,
                 StringExpression, CallExpression, MemberCallExpression>;
struct SourceAssignment {
  analysis::ScalarSlot target;
  SourceExpression value;
};
struct SourceMemberStore {
  analysis::ScalarValue object;
  abc::PoolIndex name;
  analysis::ScalarValue value;
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
using SourceOperation =
    std::variant<analysis::NoOperation, SourceAssignment, SourceMemberStore,
                 SourceConditionalBreak, SourceBreak, SourceReturn>;
struct SourceStatement {
  abc::CodeOffset pc;
  SourceOperation operation;
  // A fused property read and optional dead spill/reload share the call span.
  // Their PCs are retained, never recovered by searching generated text.
  std::optional<abc::CodeOffset> fused_read;
  std::optional<abc::CodeOffset> fused_spill;
  std::optional<abc::CodeOffset> fused_reload;
};
struct SourceAst {
  analysis::SemanticRegionTree regions;
  std::vector<SourceStatement> statements;
  std::vector<std::optional<analysis::BlockId>> fallthrough;
  std::size_t allocated_bytes = 0;
};
enum class SourceError : std::uint8_t {
  kUnsupportedOperation,
  kMissingTarget,
  kInvalidEffect,
  kResourceLimit,
  kConstant,
  kOutput,
};
struct SourceFailure {
  SourceError kind;
  abc::CodeOffset pc;
};
using SourceAstResult = std::variant<SourceAst, SourceFailure>;

// Source nodes retain exactly one PC from semantic IR; region nodes encode
// control structure without copying effectful instructions. Unsupported
// effects fail before any text is published.
[[nodiscard]] SourceAstResult BuildSourceAst(
    const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof,
    analysis::SemanticRegionTree regions, std::size_t budget_bytes);

struct GeneratedPosition {
  std::size_t byte = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 0;
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
enum class SourceForm : std::uint8_t { kFunction, kClassConstructor };

// The printer records each span as it emits its AST node, including zero-width
// anchors for elided NOPs. No source-text search or sourceMap input is used.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> PrintSourceAst(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, std::size_t output_bytes);

// Caller supplies distinct private register bindings in an already validated
// strict callable. This emits only its body; spans are relative to that body.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> PrintSourceBody(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_SOURCE_H_
