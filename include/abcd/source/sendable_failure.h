#ifndef ABCD_SOURCE_SENDABLE_FAILURE_H_
#define ABCD_SOURCE_SENDABLE_FAILURE_H_
#include "abcd/abc/class_buffer.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/module_info.h"
#include "abcd/analysis/class_creation.h"
namespace abcd::abc { enum class SendableFieldError : std::uint8_t; }
namespace abcd::source {
enum class SendableError : std::uint8_t {
  kOutputPolicy, kPreparationGate, kOwner, kCreation, kPool, kClassLiteral,
  kFieldTypes, kTypeSpelling, kEnvironment, kCapture, kHeritage, kBinding,
  kReadProof, kCorrespondence, kResourceLimit, kWorkLimit
};
using SendableCause = std::variant<std::monostate, abc::MetadataError,
    abc::LiteralError, abc::ClassBufferError, abc::SendableFieldError,
    abc::BodyError, analysis::CreationError>;
// Diagnostics carry identities/counts only; member/field arrays are separately
// charged metadata. A PC is present only for the original creation instruction.
struct SendableFailure {
  SendableError kind = SendableError::kPreparationGate;
  abc::EntityOffset record{}, method{};
  std::optional<abc::CodeOffset> pc, working_pc;
  std::optional<abc::EntityOffset> constructor, literal, field_types;
  std::optional<abc::PoolIndex> pool;
  std::optional<std::uint32_t> field, mask, field_count, instance_count, member_count;
  SendableCause cause;
  bool operator==(const SendableFailure&) const = default;
};
enum class SharedModuleError : std::uint8_t {
  kOutputPolicy, kPreparationGate, kOwner, kAttributes, kModuleTable,
  kBinding, kExport, kCorrespondence, kResourceLimit, kWorkLimit
};
struct SharedModuleFailure {
  SharedModuleError kind = SharedModuleError::kPreparationGate;
  abc::EntityOffset record{};
  std::optional<bool> shared;
  std::optional<abc::EntityOffset> module, request_phases;
  std::uint32_t requests = 0, imports = 0, namespace_imports = 0,
      local_exports = 0, indirect_exports = 0, star_exports = 0;
  std::variant<std::monostate, abc::MetadataError, abc::ModuleError> cause;
  bool operator==(const SharedModuleFailure&) const = default;
};
}
#endif
