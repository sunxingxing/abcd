#include "abcd/abc/debug_scopes.h"

#include <bit>
#include <limits>
#include <map>
#include <stdexcept>

#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
// runtime_core 3e147bb libpandafile/file_items.h. Do not reuse DWARF/Dex
// decoding: this format splits operands across program and constant pool.
enum class DebugOpcode : std::uint8_t {
  kEnd = 0,
  kAdvancePc = 1,
  kAdvanceLine = 2,
  kStart = 3,
  kStartExtended = 4,
  kEndLocal = 5,
  kRestart = 6,
  kPrologue = 7,
  kEpilogue = 8,
  kFile = 9,
  kSource = 10,
  kColumn = 11,
};
constexpr std::uint8_t kSpecialBase = 12;
constexpr std::int32_t kLineRange = 15;
constexpr std::int32_t kLineBase = -4;

class ScopeReader {
 public:
  ScopeReader(const corpus::AbcFile& file, const Header& header,
              const DebugInfo& debug, std::uint32_t code_bytes,
              DebugScopeBudget budget)
      : program_(file, debug.line_program.value, header.file_size),
        pool_(file, debug.constant_pool.value,
              debug.constant_pool.value + debug.constant_pool_bytes),
        file_bytes_(header.file_size),
        code_bytes_(code_bytes),
        budget_(budget) {
    // Dynamic emitter INVALID_LINE=-1 is serialized as UINT32_MAX. Keep
    // the signed line accumulator so a later delta can recover a real line.
    line_ = std::bit_cast<std::int32_t>(debug.line_start);
  }
  DebugScopes Read() {
    Line(0);
    Append(scopes_.positions, position_);
    for (std::size_t event = 0; event < budget_.events; ++event) {
      const auto opcode = Take(program_.U8());
      if (opcode >= kSpecialBase) {
        const auto adjusted = opcode - kSpecialBase;
        Pc(adjusted / kLineRange);
        Line(adjusted % kLineRange + kLineBase);
        Append(scopes_.positions, position_);
        continue;
      }
      switch (static_cast<DebugOpcode>(opcode)) {
        case DebugOpcode::kEnd:
          scopes_.terminal_pc = position_.pc;
          scopes_.terminal_file = position_.file;
          scopes_.terminal_source_code = position_.source_code;
          return std::move(scopes_);
        case DebugOpcode::kAdvancePc:
          Pc(Take(pool_.Uleb()));
          break;
        case DebugOpcode::kAdvanceLine:
          Line(std::bit_cast<std::int32_t>(Take(pool_.SlebBits())));
          break;
        case DebugOpcode::kStart:
          Start(false);
          break;
        case DebugOpcode::kStartExtended:
          Start(true);
          break;
        case DebugOpcode::kEndLocal:
          End();
          break;
        case DebugOpcode::kRestart:
          throw DebugScopeError::kRestartUnsupported;
        case DebugOpcode::kPrologue:
        case DebugOpcode::kEpilogue:
          break;
        case DebugOpcode::kFile:
          position_.file = Reference(true);
          break;
        case DebugOpcode::kSource:
          position_.source_code = Reference(true);
          break;
        case DebugOpcode::kColumn:
          position_.column = Take(pool_.Uleb());
          Append(scopes_.positions, position_);
          break;
      }
    }
    throw DebugScopeError::kResourceLimit;
  }
  EntityOffset Position() const { return {program_.Position()}; }

 private:
  template <typename T>
  T Take(std::variant<T, MetadataError> value) {
    if (const auto* error = std::get_if<MetadataError>(&value))
      throw DebugScopeFailure{Position(), *error};
    return std::get<T>(value);
  }
  void Charge(std::size_t bytes) {
    if (bytes > budget_.bytes - spent_) throw DebugScopeError::kResourceLimit;
    spent_ += bytes;
  }
  template <typename T>
  void Append(std::vector<T>& entries, T value) {
    if (entries.size() == entries.capacity()) {
      const auto old = entries.capacity();
      if (old > (std::numeric_limits<std::size_t>::max() - 1) / 2)
        throw DebugScopeError::kResourceLimit;
      const auto target = old * 2 + 1;
      if (target > (budget_.bytes - spent_) / sizeof(T))
        throw DebugScopeError::kResourceLimit;
      // Account for old+new storage during reallocation, not just final size.
      entries.reserve(target);
      if (entries.capacity() > (budget_.bytes - spent_) / sizeof(T))
        throw DebugScopeError::kResourceLimit;
      Charge((entries.capacity() - old) * sizeof(T));
    }
    entries.push_back(std::move(value));
  }
  std::optional<EntityOffset> Reference(bool nullable) {
    const auto value = Take(pool_.Uleb());
    if (value == 0 && nullable) return std::nullopt;
    if (value < kDynamicHeaderSize || value >= file_bytes_)
      throw DebugScopeFailure{Position(), MetadataError::kReference};
    return EntityOffset{value};
  }
  std::int32_t Register() {
    return std::bit_cast<std::int32_t>(Take(program_.SlebBits()));
  }
  void Pc(std::uint32_t increment) {
    if (increment > code_bytes_ - position_.pc) throw DebugScopeError::kPcRange;
    position_.pc += increment;
  }
  void Line(std::int32_t increment) {
    constexpr std::int64_t kMissingLine = -1;
    const auto value = line_ + increment;
    if (value < kMissingLine ||
        value > std::numeric_limits<std::int32_t>::max())
      throw DebugScopeError::kLineRange;
    line_ = value;
    if (value == kMissingLine)
      position_.line.reset();
    else
      position_.line = static_cast<std::uint32_t>(value);
  }
  void Start(bool extended) {
    DebugLocal local;
    local.reg = Register();
    local.name = *Reference(false);
    local.type = *Reference(false);
    if (extended) local.signature = Reference(false);
    local.start = position_.pc;
    auto found = active_.find(local.reg);
    if (found == active_.end()) {
      // Conservative node allowance; process ceiling also includes allocator
      // overhead. Retain one slot per signed register, not per lifetime.
      constexpr std::size_t kNodeAllowance = 64;
      Charge(sizeof(decltype(active_)::value_type) + kNodeAllowance);
      found = active_.emplace(local.reg, std::nullopt).first;
    }
    if (found->second) throw DebugScopeError::kLocalState;
    const auto index = scopes_.locals.size();
    Append(scopes_.locals, local);
    found->second = index;
  }
  void End() {
    const auto found = active_.find(Register());
    if (found == active_.end() || !found->second)
      throw DebugScopeError::kLocalState;
    scopes_.locals[*found->second].end = position_.pc;
    found->second.reset();
  }
  MetadataCursor program_;
  MetadataCursor pool_;
  std::uint32_t file_bytes_;
  std::uint32_t code_bytes_;
  DebugScopeBudget budget_;
  std::size_t spent_ = 0;
  DebugPosition position_;
  std::int64_t line_ = 0;
  DebugScopes scopes_;
  std::map<std::int32_t, std::optional<std::size_t>> active_;
};
bool Supported(ProfileId profile) {
  switch (profile) {
    case ProfileId::kDynamic9:
    case ProfileId::kDynamic1102:
    case ProfileId::kDynamic1202:
    case ProfileId::kDynamic1206:
    case ProfileId::kDynamic1301:
    case ProfileId::kDynamic2400:
      return true;
    case ProfileId::kUnknown:
    case ProfileId::kStatic0107:
      return false;
  }
  return false;
}
}  // namespace

DebugScopeResult ReadDebugScopes(const corpus::AbcFile& file,
                                 const Header& header, const DebugInfo& debug,
                                 std::uint32_t code_bytes,
                                 DebugScopeBudget budget) {
  if (header.kind != AbcKind::kDynamic || !Supported(header.profile) ||
      !std::holds_alternative<DynamicLayout>(header.layout))
    return DebugScopeFailure{debug.line_program,
                             MetadataError::kUnsupportedProfile};
  if (header.file_size != file.Size() || file.CheckUnchanged())
    return DebugScopeFailure{debug.line_program, MetadataError::kFileChanged};
  if (debug.line_program.value < kDynamicHeaderSize ||
      debug.line_program.value >= header.file_size ||
      debug.constant_pool.value < kDynamicHeaderSize ||
      debug.constant_pool.value > header.file_size ||
      debug.constant_pool_bytes > header.file_size - debug.constant_pool.value)
    return DebugScopeFailure{debug.line_program, MetadataError::kReference};
  ScopeReader reader(file, header, debug, code_bytes, budget);
  try {
    auto scopes = reader.Read();
    if (file.CheckUnchanged())
      return DebugScopeFailure{debug.line_program, MetadataError::kFileChanged};
    return scopes;
  } catch (const DebugScopeFailure& failure) {
    return failure;
  } catch (DebugScopeError error) {
    return DebugScopeFailure{reader.Position(), error};
  } catch (const std::bad_alloc&) {
    return DebugScopeFailure{reader.Position(),
                             DebugScopeError::kResourceLimit};
  } catch (const std::length_error&) {
    return DebugScopeFailure{reader.Position(),
                             DebugScopeError::kResourceLimit};
  }
}
}  // namespace abcd::abc
