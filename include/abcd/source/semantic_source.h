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
#include "abcd/abc/class_buffer.h"
#include "abcd/analysis/class_heritage.h"
#include "abcd/analysis/semantic_region.h"
#include "abcd/analysis/semantic_validation.h"
#include "abcd/source/linear_body.h"
#include "abcd/source/source_contract.h"

namespace abcd::analysis { struct ExceptionLocals; }
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
  analysis::CaptureDomain requirement = analysis::CaptureDomain::kLanguage;
};
struct LocalExpression { analysis::LocalCellId cell; };
struct FunctionExpression {
  analysis::FunctionCreation creation;
  analysis::InstructionId instruction;
  std::optional<analysis::InstructionId> previous_creation;
  std::optional<analysis::ExceptionNodeId> capture_site = {};
};
struct SourceClassMember {
  analysis::FunctionCreation creation;
  abc::PoolIndex key;
  abc::ClassPlacement placement;
  abc::ClassMethodKind kind;
};
struct ClassExpression {
  analysis::ClassCreation creation;
  abc::CodeOffset origin;
  std::optional<abc::PoolIndex> prototype_key = {};
  std::vector<SourceClassMember> assembled_members = {};
  analysis::HeritageKind heritage = analysis::HeritageKind::kUnknown;
  std::optional<analysis::FunctionCreation> initializer = {};
  // Normal-SSA creation site; folded members share its lexical environment.
  std::optional<analysis::InstructionId> capture_instruction = {};
};
// These nodes preserve assembly operations before class ownership is proved.
// They are not ordinary function literals or calls to mutable JS intrinsics.
struct MethodExpression {
  analysis::FunctionCreation creation;
  analysis::InstructionId instruction;
  analysis::Accumulator home;
};
struct AccessorExpression {
  analysis::DefineAccessor definition;
  analysis::Accumulator naming_flag;
};
using SourceExpression =
    std::variant<analysis::ScalarExpression, MemberExpression,
                 StringExpression, CallExpression, MemberCallExpression,
                 ModuleExpression, analysis::EmptyObjectCreation,
                 analysis::BufferedObjectCreation, GlobalExpression,
                 analysis::ConstructorCall, CapturedExpression,
                 FunctionExpression, LocalExpression, analysis::RestCopy,
                 analysis::BufferedArrayCreation, analysis::AwaitValue,
                 analysis::SuperRange, ClassExpression, MethodExpression,
                 AccessorExpression, analysis::GlobalObjectLoad>;
struct SourceAssignment {
  analysis::ScalarSlot target;
  SourceExpression value;
};
struct SourceMemberStore {
  analysis::ScalarValue object;
  abc::PoolIndex name;
  std::variant<analysis::ScalarValue, StringExpression> value;
};
struct SourceLocalStore {
  analysis::LocalCellId cell;
  analysis::ScalarSlot value;
};
struct SourceLocalAssignment {
  analysis::LocalCellId cell;
  SourceExpression value;
};
struct SourceModuleStore {
  analysis::ModuleSlot slot;
  analysis::ScalarSlot value;
};
struct SourceOwnStore {
  analysis::OwnWrite write;
  analysis::BufferedObjectCreation origin;
};
struct SourceMethodStore {
  analysis::OwnWrite write;
  analysis::InstructionId creation;
  // Same SSA value as the creation's home, modulo normal scalar copies.
  // This is NOT a class ownership, descriptor or non-escape certificate.
  analysis::SsaValueId home;
};
struct SourceIndexedStore {
  analysis::IndexedWrite write;
  analysis::Accumulator value;
};
struct SourceArrayCapture { analysis::InstructionId element; };
struct SourceArrayCommit {
  abc::RegisterId target;
  std::vector<analysis::InstructionId> elements;
};
// The owner proves a private same-block array with one final read. Element
// intervals contain assignments/no-ops only and retain original PC anchors.
struct SourceArrayLiteral {
  abc::RegisterId target;
  analysis::InstructionId save;
  std::vector<analysis::InstructionId> elements;
};
struct SourceConditionalBreak {
  analysis::UnaryExpression condition;
  analysis::BlockId target;
  // Proved replacement for condition.input; never parse it from source text.
  std::optional<analysis::ScalarExpression> input = {};
};
struct SourceBreak {
  analysis::BlockId target;
};
struct SourceConditionalContinue {
  analysis::UnaryExpression condition;
  analysis::BlockId target;
  std::optional<analysis::ScalarExpression> input = {};
};
struct SourceContinue {
  analysis::BlockId target;
};
struct SourceReturn {
  analysis::ScalarValue value;
};
struct SourceReturnExpression {
  SourceExpression value;
};
struct SourceThrow {
  analysis::ScalarValue value;
};
struct SourceStagedCall {
  abc::PoolIndex name;
  analysis::ReceiverCall call;
  analysis::InstructionId first_argument_statement;
  analysis::InstructionId call_statement;
};
// Unprintable until same-home, non-escape assembly and initializer-body proof.
struct SourceInitializerCall { analysis::ReceiverCall call; };
using SourceOperation =
    std::variant<analysis::NoOperation, SourceAssignment, SourceMemberStore, SourceOwnStore,
                 SourceLocalStore, SourceLocalAssignment, SourceModuleStore, SourceMethodStore,
                 SourceIndexedStore,
                 SourceArrayCapture, SourceArrayCommit, SourceArrayLiteral,
                 SourceConditionalBreak, SourceBreak, SourceConditionalContinue,
                 SourceContinue, SourceReturn, SourceReturnExpression, SourceThrow,
                 SourceStagedCall, SourceInitializerCall>;
struct SourceStatement {
  abc::CodeOffset pc;
  SourceOperation operation;
  // A fused expression and optional dead spill/reload share the consumer span.
  // Their PCs are retained, never recovered by searching generated text.
  std::optional<abc::CodeOffset> fused_read;
  std::optional<abc::CodeOffset> fused_spill;
  std::optional<abc::CodeOffset> fused_reload;
  std::size_t read_distance = 0;
  std::size_t spill_distance = 0;
  std::size_t reload_distance = 0;
  std::optional<abc::CodeOffset> previous_origin;
  // Assembly operations folded into an earlier class share its generated span.
  std::optional<analysis::InstructionId> class_origin = {};
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
  std::size_t array_captures = 0;
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
  kClassAssembly,
  kOwnProperty,
  kRealmBinding,
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
// derived is the unchanged same-body/frame ProveRangeDerivedConstructor
// certificate. It licenses native super and omission of proved VM checks only
// inside the caller's actual derived constructor, never an ordinary function.
[[nodiscard]] SourceAstResult BuildSourceAst(
    const analysis::SemanticMethod& method,
    const analysis::SemanticProof& proof,
    analysis::SemanticRegionTree regions, std::size_t budget_bytes,
    std::span<const std::uint8_t> named_scopes = {},
    std::optional<analysis::RestPrologue> rest = {},
    std::optional<analysis::RangeDerivedConstructor> derived = {});

struct GeneratedPosition {
  std::size_t byte = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 0;
  // Template emission cursor: distinguishes before/after a zero-width
  // capture at this byte. Always zero in fully bound source coordinates.
  std::size_t captures = 0;
};
enum class SpanOrigin : std::uint8_t { kInstruction, kAwaitProtocol };
// Serialization only; engine decisions use SpanOrigin, never these labels.
constexpr std::string_view SpanOriginName(SpanOrigin origin) {
  switch (origin) {
    case SpanOrigin::kInstruction: return "instruction";
    case SpanOrigin::kAwaitProtocol: return "await_protocol";
  }
  std::terminate();
}
struct GeneratedSpan {
  abc::CodeOffset pc;
  GeneratedPosition begin;
  GeneratedPosition end;
  SpanOrigin origin = SpanOrigin::kInstruction;
};
struct HostRealmRequirement {
  // ASCII lexical name, owned by the artifact, not a JS expression.
  std::string binding;
};
struct PrintedSource {
  std::string text;
  std::vector<GeneratedSpan> spans;
  SourceContract contract = kSourceContract;
  std::optional<HostRealmRequirement> host_realm;
};
struct SourceCapture {
  GeneratedPosition position;
  analysis::CapturedSlot cell;
  analysis::CaptureDomain requirement = analysis::CaptureDomain::kLanguage;
};
// Not executable source. Positions are template coordinates, prior to capture
// insertion; never expose them as final generated-file positions.
struct PrintedTemplate {
  std::string text;
  std::vector<GeneratedSpan> spans;
  std::vector<SourceCapture> captures;
  SourceContract contract = kSourceContract;
  std::optional<HostRealmRequirement> host_realm;
};
// source is the unchanged PrintSourceTemplate result. bindings is one
// parent-PROVEN initialized local cell per capture occurrence,
// in emission order, rebased at this exact creation site. The owning scope
// declares the private $lex<environment>_<slot> bindings without collisions.
// This Language-only binding API rejects stronger capture requirements.
// This substitutes bindings, not initialization/capture proof. No source text
// is parsed to recover positions. O(text + C + S log C), bounded scratch.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> BindSourceTemplate(
    const PrintedTemplate& source,
    std::span<const analysis::LocalCell> bindings,
    std::size_t budget_bytes);
enum class SourceForm : std::uint8_t { kFunction, kClassConstructor, kRecord };

// The printer records each span as it emits its AST node, including zero-width
// anchors for elided NOPs. No source-text search or sourceMap input is used.
// Record form requires an owner-validated ESM entry, zero parameters, no
// observed incoming ABI values, and fresh same-record exports. Entry-derived
// values are independently restricted to erased scalar copies. The export plan
// is mutated during printing, including failed attempts: do not reuse it.
// Other forms must not supply record_exports. No partial source is returned.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> PrintSourceAst(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, std::size_t output_bytes,
    const analysis::SemanticProof* proof = nullptr,
    ModuleExports* record_exports = nullptr);

// Caller supplies distinct private register bindings in an already validated
// strict callable. This emits only its body; spans are relative to that body.
// Optional proof belongs to this unchanged method/AST and remains live during
// printing. It enables binding child captures to proven initialized locals;
// absence never authorizes guessed captures. Child-internal spans are separate.
// Nonempty constants.awaits requires an owning native async callable; this
// body-only API does not emit or prove the surrounding callable syntax.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> PrintSourceBody(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes,
    const analysis::SemanticProof* proof = nullptr);

// Common layout-only printer for a same-body, already verified AST, including
// BuildExceptionSourceAst results. It cannot bind child captures using a
// normal-CFG proof. All binding/async/position contracts above still apply.
// Optional phased environments/locals must match the verified AST and outlive
// printing; only their complete-graph capture certificate permits local binds.
[[nodiscard]] std::variant<PrintedSource, SourceFailure> PrintSourceBody(
    const SourceAst& ast, const analysis::NormalFlow& blocks,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes,
    const analysis::EnvironmentFlow* environments = nullptr,
    const analysis::ExceptionLocals* locals = nullptr);
// Captured expressions become typed insertions, not invented identifiers.
// Only a parent capture-binding path may consume this incomplete artifact.
// Every occurrence's required domain must be discharged without weakening it.
[[nodiscard]] std::variant<PrintedTemplate, SourceFailure> PrintSourceTemplate(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes,
    const analysis::SemanticProof* proof = nullptr);
// Layout-only counterpart for a verified phased AST. Optional environments
// must be the unchanged phased owner used to build this AST and outlive print.
// Child captures can be rebased to outer-cell insertions; locally allocated
// cells require the optional complete-phase initialization certificate.
[[nodiscard]] std::variant<PrintedTemplate, SourceFailure> PrintSourceTemplate(
    const SourceAst& ast, const analysis::NormalFlow& blocks,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes,
    const analysis::EnvironmentFlow* environments = nullptr,
    const analysis::ExceptionLocals* locals = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_SEMANTIC_SOURCE_H_
