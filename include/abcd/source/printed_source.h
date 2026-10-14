#ifndef ABCD_SOURCE_PRINTED_SOURCE_H_
#define ABCD_SOURCE_PRINTED_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "abcd/abc/method_body.h"
#include "abcd/abc/metadata_index.h"
#include "abcd/source/cleanup_origin.h"
#include "abcd/source/parameter_default.h"
#include "abcd/source/source_contract.h"
#include "abcd/source/source_text.h"

// Published artifacts of the value-tree source route: typed failures,
// generated positions and the printed record/function text with its maps.
namespace abcd::source {
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
struct GeneratedPosition {
  std::size_t byte = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 0;
  // Template emission cursor: distinguishes before/after a zero-width
  // capture at this byte. Always zero in fully bound source coordinates.
  std::size_t captures = 0;
};
class FunctionEmission;
class FamilyEmission;
// A separately verified method or function family in a possibly legacy container.
// Only the envelope moves during replay/binding; immutable trees, local byte
// ranges, origin DAGs and receipts remain paired. No template insertion captures
// may occur inside it; native lexical captures stay in the owned family AST.
// Container coverage is independent of these method receipts.
struct FunctionSegment {
  std::variant<std::shared_ptr<const FunctionEmission>, std::shared_ptr<const FamilyEmission>> function;
  GeneratedPosition begin;
  GeneratedPosition end;
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
struct MethodGeneratedSpan {
  abc::EntityOffset method;
  GeneratedSpan span;
};
struct HostRealmRequirement {
  // ASCII lexical name, owned by the artifact, not a JS expression.
  std::string binding;
};
enum class VmSlotUse : std::uint8_t { kUnknown, kNone, kRequired };
enum class RegisterUse : std::uint8_t { kUnused, kUsed };
struct PrintedSource {
  SourceText text;
  std::vector<GeneratedSpan> spans;
  SourceContract contract = kSourceContract;
  std::optional<HostRealmRequirement> host_realm;
  // Partial descendant coverage, never evidence that every method is mapped.
  std::vector<MethodGeneratedSpan> method_spans;
  VmSlotUse vm_slots = VmSlotUse::kUnknown;
  bool accumulator_used = false;
  std::vector<RegisterUse> register_uses;
  std::vector<ParameterDefault> parameter_defaults;
  std::vector<FunctionSegment> function_segments;
  // A retained class/member gap makes the surrounding record incomplete.
  // This is recovery status, never permission to execute a partial artifact.
  bool incomplete = false;
  std::vector<EnvironmentCleanupSite> environment_cleanup;
};
enum class SourceForm : std::uint8_t { kFunction, kRecord };
}  // namespace abcd::source
#endif  // ABCD_SOURCE_PRINTED_SOURCE_H_
