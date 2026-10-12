#include <type_traits>
#include "abcd/abc/profile_capabilities.h"

#include "abcd/abc/class_buffer.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/method_target.h"
#include "abcd/analysis/class_creation.h"
#include "abcd/analysis/class_heritage.h"
#include "abcd/analysis/define_instruction.h"
#include "record_text.h"

namespace abcd::structure {
namespace {
enum class ClassProfileError { kUnsupportedSemanticProfile };
using SiteError =
    std::variant<analysis::CreationError, abc::MetadataError,
                 abc::MethodTargetError, abc::MethodAttributeError,
                 abc::LiteralFailure, abc::ClassBufferError,
                 analysis::DefineError, ClassProfileError>;
struct SiteFailure {
  SiteError error;
};
template <typename T, typename... V>
T Take(std::variant<V...> value) {
  if (!std::holds_alternative<T>(value))
    std::visit(
        [](const auto& item) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(item)>, T>)
            throw SiteFailure{item};
        },
        value);
  return std::get<T>(std::move(value));
}
const char* Domain(analysis::CreationError) { return "creation"; }
const char* Domain(abc::MetadataError) { return "metadata"; }
const char* Domain(abc::MethodTargetError) { return "method_target"; }
const char* Domain(abc::MethodAttributeError) { return "method_attribute"; }
const char* Domain(abc::LiteralFailure) { return "literal"; }
const char* Domain(abc::ClassBufferError) { return "class_buffer"; }
const char* Domain(analysis::DefineError) { return "define"; }
const char* Domain(ClassProfileError) { return "class_profile"; }
void Error(std::ostream& out, const SiteFailure& failure) {
  std::visit(
      [&](const auto& error) {
        out << "\"error\":{\"domain\":\"" << Domain(error) << "\",\"code\":";
        if constexpr (std::is_same_v<std::decay_t<decltype(error)>,
                                     abc::LiteralFailure>)
          out << static_cast<unsigned>(error.kind);
        else
          out << static_cast<unsigned>(error);
        out << '}';
      },
      failure.error);
}
bool ClassOpcode(abc::Opcode opcode) {
  switch (opcode) {
    case abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8:
    case abc::Opcode::kDEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8:
    case abc::Opcode::
        kDEPRECATED_DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8:
    case abc::Opcode::
        kCALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8:
      return true;
    default:
      return false;
  }
}
const abc::MethodInfo& Member(const abc::MetadataIndex& index,
                              const abc::MethodLookup& lookup,
                              abc::EntityOffset target) {
  const auto position = lookup.Find(target);
  if (!position) throw SiteFailure{abc::MethodTargetError::kTargetMissing};
  if (position->value >= index.methods.size() ||
      index.methods[position->value].offset != target)
    throw SiteFailure{abc::MethodTargetError::kIndexMismatch};
  return index.methods[position->value];
}
void Literal(const RecordText& text, const abc::LiteralEntry& literal,
             std::ostream& out) {
  out << "{\"tag\":" << static_cast<unsigned>(literal.tag);
  if (const auto* ref = std::get_if<abc::EntityOffset>(&literal.value)) {
    out << ",\"text\":";
    text.String(out, *ref);
  } else
    out << ",\"bits\":" << std::get<abc::LiteralBits>(literal.value).value;
  out << '}';
}
void ValidateString(const RecordText& text, const abc::LiteralEntry& literal) {
  const auto* ref = std::get_if<abc::EntityOffset>(&literal.value);
  if (!ref) return;
  try {
    text.Read(*ref);
  } catch (const StructureFailure& failure) {
    if (const auto* error = std::get_if<abc::MetadataError>(&failure.error))
      throw SiteFailure{*error};
    throw;
  }
}
void Class(const RecordText& text, const abc::MetadataIndex& index,
           const abc::MethodLookup& lookup, const abc::MethodInfo& caller,
           const abc::DecodedInstruction& instruction, std::ostream& out,
           analysis::HeritageKind heritage) {
  const auto site =
      Take<analysis::ClassCreation>(analysis::LiftClassCreation(instruction));
  const auto constructor = Take<abc::MethodTarget>(abc::ResolveMethodTarget(
      text.file, text.header, index, lookup, caller.offset, site.constructor));
  const auto buffer = Take<abc::PoolReference>(abc::ResolvePoolReference(
      text.file, text.header, index, caller.offset, site.literal,
      abc::PoolEntityKind::kLiteralArray));
  const auto raw = Take<abc::LiteralArray>(abc::ReadLiteralArray(
      text.file, text.header, buffer.offset, {text.budget.literal_bytes}));
  const auto layout = Take<abc::ClassBuffer>(
      abc::DecodeClassBuffer(raw, {text.budget.class_bytes}));
  // Require actual method membership before publishing ownership. Do not
  // reinterpret literal method entity IDs as caller-region pool indices.
  for (const auto& property : layout.properties) {
    // Validate lazy text before any site payload is emitted. One bounded
    // string at a time; malformed text becomes this site's explicit error.
    ValidateString(text, property.key);
    if (const auto* literal = std::get_if<abc::LiteralEntry>(&property.value))
      ValidateString(text, *literal);
    if (const auto* method = std::get_if<abc::ClassMethod>(&property.value)) {
      const auto& member = Member(index, lookup, method->target);
      Take<abc::MethodAttributes>(abc::DecodeMethodAttributes(
          member.access_flags, text.header.profile));
    }
  }
  out << "\"constructor\":"
      << index.methods[constructor.position.value].offset.value
      << ",\"constructor_length\":" << site.length
      << ",\"parent_register\":" << site.parent.value << ",\"heritage\":\"";
  switch (heritage) {
    case analysis::HeritageKind::kUnknown:
      out << "unknown";
      break;
    case analysis::HeritageKind::kAbsent:
      out << "absent";
      break;
    case analysis::HeritageKind::kNull:
      out << "null";
      break;
  }
  out << "\",\"buffer\":" << buffer.offset.value << ",\"properties\":[";
  bool comma = false;
  for (const auto& property : layout.properties) {
    if (comma) out << ',';
    comma = true;
    out << "{\"placement\":\""
        << (property.placement == abc::ClassPlacement::kStatic ? "static"
                                                               : "prototype")
        << "\",\"key\":";
    Literal(text, property.key, out);
    if (const auto* method = std::get_if<abc::ClassMethod>(&property.value)) {
      const auto& member = Member(index, lookup, method->target);
      const auto attributes =
          Take<abc::MethodAttributes>(abc::DecodeMethodAttributes(
              member.access_flags, text.header.profile));
      out << ",\"method\":{\"entity\":" << method->target.value
          << ",\"record\":" << member.owner_offset.value
          << ",\"template_kind\":" << static_cast<unsigned>(method->kind)
          << ",\"initial_kind\":" << static_cast<unsigned>(attributes.kind)
          << ",\"length\":" << method->length << '}';
    } else {
      out << ",\"literal\":";
      Literal(text, std::get<abc::LiteralEntry>(property.value), out);
    }
    out << '}';
  }
  out << ']';
}
void IncompleteClass(const RecordText& text, const abc::MetadataIndex& index,
                     const abc::MethodLookup& lookup,
                     const abc::MethodInfo& caller,
                     const abc::DecodedInstruction& instruction,
                     std::ostream& out, const abc::FrameLayout* frame) {
  // Class-literal layout without header literal arrays, under pinned semantics.
  if (!abc::HasPinnedRuntimeSemantics(text.header.profile) ||
      abc::CapabilitiesOf(text.header.profile).literal_arrays_in_header)
    throw SiteFailure{ClassProfileError::kUnsupportedSemanticProfile};
  const auto lifted = analysis::LiftDefineInstruction(
      instruction, frame ? frame->registers : UINT64_MAX);
  abc::PoolIndex constructor_pool;
  abc::PoolIndex literal_pool;
  std::uint16_t length;
  abc::RegisterId parent;
  if (const auto* sendable =
          std::get_if<analysis::DefineSendableClass>(&lifted)) {
    constructor_pool = sendable->constructor;
    literal_pool = sendable->literal;
    length = sendable->length;
    parent = sendable->parent;
  } else if (const auto* legacy =
                 std::get_if<analysis::DefineLegacyClass>(&lifted)) {
    constructor_pool = legacy->constructor;
    // The pinned legacy handler reads the buffer from constructor slot + 1;
    // encoded_literal remains evidence, not a replacement pool index.
    constexpr std::uint32_t kLegacyLiteralSlotOffset = 1;
    literal_pool = {constructor_pool.value + kLegacyLiteralSlotOffset};
    length = legacy->length;
    parent = legacy->parent;
  } else if (const auto* error = std::get_if<analysis::DefineError>(&lifted)) {
    throw SiteFailure{*error};
  } else {
    throw SiteFailure{analysis::DefineError::kUnsupportedOpcode};
  }
  const auto constructor = Take<abc::MethodTarget>(abc::ResolveMethodTarget(
      text.file, text.header, index, lookup, caller.offset, constructor_pool));
  const auto buffer = Take<abc::PoolReference>(abc::ResolvePoolReference(
      text.file, text.header, index, caller.offset, literal_pool,
      abc::PoolEntityKind::kLiteralArray));
  if (const auto* sendable =
          std::get_if<analysis::DefineSendableClass>(&lifted)) {
    out << "\"form\":\"sendable\",\"reserved_operand\":"
        << sendable->reserved_operand << ',';
  } else {
    const auto& legacy = std::get<analysis::DefineLegacyClass>(lifted);
    out << "\"form\":\"legacy\",\"encoded_literal\":"
        << legacy.encoded_literal << ",\"lexical_environment_register\":"
        << legacy.lexical_environment.value << ',';
  }
  out << "\"constructor\":"
      << index.methods[constructor.position.value].offset.value
      << ",\"constructor_pool_index\":" << constructor_pool.value
      << ",\"constructor_length\":" << length
      << ",\"literal_pool_index\":" << literal_pool.value
      << ",\"parent_register\":" << parent.value
      << ",\"buffer\":" << buffer.offset.value
      << ",\"heritage\":\"unknown\",\"properties\":null"
      << ",\"source_admission\":false";
}
}  // namespace

void WriteClassSites(const RecordText& text, const abc::MetadataIndex& index,
                     const abc::MethodLookup& lookup,
                     const abc::MethodInfo& method, std::ostream& out,
                     const abc::FrameLayout* frame) {
  out << ",\"class_sites\":";
  if (!method.code) {
    out << "null,\"property_writes\":null,\"global_accesses\":null";
    return;
  }
  const auto decoded = abc::DecodeMethodBody(
      text.file, text.header.profile, *method.code, {text.budget.body_bytes});
  if (const auto* failure = std::get_if<abc::BodyFailure>(&decoded)) {
    if (failure->kind == abc::BodyError::kFile)
      throw StructureFailure{method.offset, abc::MetadataError::kRead};
    out << "null,\"class_scan_error\":{\"code\":"
        << static_cast<unsigned>(failure->kind)
        << ",\"pc\":" << failure->offset.value
        << "},\"property_writes\":null,\"global_accesses\":null";
    return;
  }
  const auto& body = std::get<abc::MethodBody>(decoded);
  std::variant<std::vector<analysis::ClassHeritage>, analysis::HeritageError>
      heritage = std::vector<analysis::ClassHeritage>{};
  if (frame)
    heritage = analysis::ProveClassHeritage(body, frame->registers,
                                            {text.budget.heritage_bytes});
  const auto* facts =
      std::get_if<std::vector<analysis::ClassHeritage>>(&heritage);
  std::size_t fact = 0;
  out << '[';
  bool comma = false;
  for (const auto& instruction :
       std::get<abc::MethodBody>(decoded).instructions) {
    if (!ClassOpcode(instruction.decoded.opcode)) continue;
    auto kind = analysis::HeritageKind::kUnknown;
    if (facts) {
      while (fact < facts->size() &&
             (*facts)[fact].pc.value < instruction.offset.value)
        ++fact;
      if (fact < facts->size() && (*facts)[fact].pc == instruction.offset)
        kind = (*facts)[fact].kind;
    }
    if (comma) out << ',';
    comma = true;
    out << "{\"pc\":" << instruction.offset.value << ',';
    try {
      switch (instruction.decoded.opcode) {
        case abc::Opcode::
            kCALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8:
        case abc::Opcode::
            kDEPRECATED_DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8:
          IncompleteClass(text, index, lookup, method, instruction.decoded,
                          out, frame);
          break;
        default:
          Class(text, index, lookup, method, instruction.decoded, out, kind);
          break;
      }
    } catch (const SiteFailure& failure) {
      if (const auto* error = std::get_if<abc::MetadataError>(&failure.error))
        if (*error == abc::MetadataError::kRead ||
            *error == abc::MetadataError::kFileChanged)
          throw StructureFailure{method.offset, *error};
      if (const auto* error = std::get_if<abc::LiteralFailure>(&failure.error))
        if (error->kind == abc::LiteralError::kRead ||
            error->kind == abc::LiteralError::kFileChanged)
          throw StructureFailure{method.offset, abc::MetadataError::kRead};
      Error(out, failure);
    }
    out << '}';
  }
  out << ']';
  if (const auto* error = std::get_if<analysis::HeritageError>(&heritage))
    out << ",\"heritage_scan_error\":{\"code\":"
        << static_cast<unsigned>(*error) << '}';
  WritePropertySites(text, index, method, std::get<abc::MethodBody>(decoded),
                     out, frame);
  WriteGlobalSites(text, index, method, std::get<abc::MethodBody>(decoded),
                   out);
}
}  // namespace abcd::structure
