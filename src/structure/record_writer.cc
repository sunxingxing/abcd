#include "abcd/structure/record_writer.h"

#include <streambuf>

#include "abcd/abc/call_type.h"
#include "abcd/corpus/abc_file.h"
#include "record_text.h"

namespace abcd::structure {
std::u16string RecordText::Read(abc::EntityOffset offset) const {
  auto value =
      abc::ReadStringEntity(file, header, offset, {budget.string_bytes});
  if (const auto* error = std::get_if<abc::MetadataError>(&value))
    throw StructureFailure{offset, *error};
  return std::get<std::u16string>(std::move(value));
}
void RecordText::String(std::ostream& out, std::u16string_view value) const {
  const auto result =
      source::WriteStringLiteral(value, out, {budget.output_bytes});
  if (const auto* error = std::get_if<source::EmitError>(&result))
    throw StructureFailure{{}, *error};
}
void RecordText::String(std::ostream& out, abc::EntityOffset offset) const {
  String(out, Read(offset));
}
namespace {
// Quota exhaustion never reaches the target streams; write_failed records a
// real short write by a target buffer and takes precedence over any later
// structural error, so an output fault is never reported as a record fault.
struct OutputQuota {
  std::size_t remaining;
  bool exceeded = false;
  bool write_failed = false;
};
class OutputBuffer : public std::streambuf {
 public:
  OutputBuffer(std::ostream& target, OutputQuota& quota)
      : target_(target.rdbuf()), quota_(quota) {}

 private:
  std::streamsize xsputn(const char* data, std::streamsize count) override {
    if (count < 0 || static_cast<std::size_t>(count) > quota_.remaining) {
      quota_.exceeded = true;
      return 0;
    }
    const auto written = target_->sputn(data, count);
    if (written < count) quota_.write_failed = true;
    if (written > 0) quota_.remaining -= static_cast<std::size_t>(written);
    return written;
  }
  int_type overflow(int_type value) override {
    if (traits_type::eq_int_type(value, traits_type::eof()))
      return traits_type::not_eof(value);
    const auto byte = traits_type::to_char_type(value);
    return xsputn(&byte, 1) == 1 ? value : traits_type::eof();
  }
  std::streambuf* target_;
  OutputQuota& quota_;
};
const char* Kind(abc::FunctionKind kind) {
  switch (kind) {
    case abc::FunctionKind::kNone:
      return "none";
    case abc::FunctionKind::kBaseConstructor:
      return "base_constructor";
    case abc::FunctionKind::kArrow:
      return "arrow";
    case abc::FunctionKind::kGenerator:
      return "generator";
    case abc::FunctionKind::kAsync:
      return "async";
    case abc::FunctionKind::kAsyncGenerator:
      return "async_generator";
    case abc::FunctionKind::kAsyncArrow:
      return "async_arrow";
    case abc::FunctionKind::kConcurrent:
      return "concurrent";
  }
  std::terminate();
}
void OptionalOffset(std::ostream& out,
                    std::optional<abc::EntityOffset> offset) {
  if (offset)
    out << offset->value;
  else
    out << "null";
}
void Flag(std::ostream& out, std::optional<bool> value) {
  if (value)
    out << (*value ? "true" : "false");
  else
    out << "null";
}
void WriteMethod(const RecordText& text, const abc::MetadataIndex& index,
                 const abc::MethodLookup& lookup, const abc::MethodInfo& method,
                 std::ostream& out, std::ostream& declaration) {
  const auto parsed =
      abc::DecodeMethodAttributes(method.access_flags, text.header.profile);
  if (const auto* error = std::get_if<abc::MethodAttributeError>(&parsed))
    throw StructureFailure{method.offset, *error};
  const auto attributes = std::get<abc::MethodAttributes>(parsed);
  const auto name = text.Read(method.name_offset);
  out << "{\"entity\":" << method.offset.value << ",\"encoded_name\":";
  text.String(out, name);
  out << ",\"runtime_name\":";
  bool ascii = true;
  for (const auto ch : name) ascii &= ch != 0 && ch < 0x80;
  if (ascii && !name.empty()) {
    std::u16string_view runtime = name;
    if (runtime.front() == u'#') {
      runtime.remove_prefix(runtime.find_last_of(u'#') + 1);
      const auto suffix = runtime.find_last_of(u'^');
      if (suffix != std::u16string_view::npos)
        runtime = runtime.substr(0, suffix);
    }
    text.String(out, runtime);
  } else
    out << "null";
  out << ",\"initial_kind\":\"" << Kind(attributes.kind)
      << "\",\"sendable\":" << (attributes.sendable ? "true" : "false")
      << ",\"debug_info\":";
  OptionalOffset(out, method.debug_info);
  out << ",\"debug\":";
  WriteDebugStructure(text, method, out);
  out << ",\"proto\":";
  OptionalOffset(out, method.proto_offset);
  out << ",\"abi\":";
  declaration << "// method " << method.offset.value << " ";
  text.String(declaration, name);
  declaration << " kind=" << Kind(attributes.kind);
  std::optional<abc::FrameLayout> method_frame;
  if (method.code) {
    const auto call =
        abc::ReadCallType(text.file, text.header, index, method.annotations,
                          {text.budget.string_bytes});
    if (const auto* error = std::get_if<abc::MetadataError>(&call))
      throw StructureFailure{method.offset, *error};
    const auto frame = abc::BuildFrameLayout(
        method.code->registers, method.code->arguments,
        abc::DecodeCallFlags(std::get<abc::CallType>(call).value));
    if (const auto* error = std::get_if<abc::FrameError>(&frame))
      throw StructureFailure{method.offset, *error};
    const auto& layout = std::get<abc::FrameLayout>(frame);
    method_frame = layout;
    out << "{\"local_slots\":" << layout.locals
        << ",\"parameter_slots\":" << layout.parameters
        << ",\"first_parameter\":" << layout.first_parameter.value
        << ",\"parameter_names\":null,\"parameter_types\":null,\"return_type\":"
           "null}";
    declaration << " parameter_slots=" << layout.parameters;
  } else
    out << "null";
  WriteClassSites(text, index, lookup, method, out,
                  method_frame ? &*method_frame : nullptr);
  out << '}';
  declaration << " (scope and source signature unresolved)\n";
}
}  // namespace

std::variant<RecordCounts, StructureFailure> WriteRecordStructure(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, abc::FilePackaging packaging,
    std::ostream& metadata, std::ostream& declarations,
    StructureBudget budget) {
  OutputQuota quota{budget.output_bytes};
  OutputBuffer json_buffer(metadata, quota),
      declaration_buffer(declarations, quota);
  std::ostream out(&json_buffer), declaration(&declaration_buffer);
  const RecordText text(file, header, budget);
  try {
    if (record.first_method > index.methods.size() ||
        record.method_count > index.methods.size() - record.first_method ||
        record.first_field > index.fields.size() ||
        record.field_count > index.fields.size() - record.first_field)
      return StructureFailure{record.offset, StructureFault::kMemberRange};
    RecordCounts counts{record.method_count, record.field_count};
    out << "{\"schema\":1,\"view\":\"structure_only\",\"entity\":"
        << record.offset.value << ",\"descriptor\":";
    text.String(out, record.offset);
    out << ",\"source_file\":";
    if (record.source_file)
      text.String(out, *record.source_file);
    else
      out << "null";
    WriteSourceIdentity(text, index, record, out);
    declaration << "// ABC structural declaration view. No function bodies "
                   "recovered.\n// record ";
    text.String(declaration, record.offset);
    declaration
        << "\n// Method inventory below does not imply module/class scope.\n";
    out << ",\"module\":";
    if (packaging == abc::FilePackaging::kMerged) {
      const auto parsed = abc::ReadMergedRecordAttributes(
          file, header, index, record, {budget.string_bytes});
      if (const auto* error = std::get_if<abc::AttributeFailure>(&parsed))
        throw StructureFailure{record.offset, *error};
      const auto& attributes = std::get<abc::RecordAttributes>(parsed);
      if (attributes.module) {
        const auto decoded = abc::ReadModuleInfo(
            file, header, *attributes.module, {budget.module_bytes});
        if (const auto* error = std::get_if<abc::ModuleFailure>(&decoded))
          throw StructureFailure{record.offset, *error};
        const auto& module = std::get<abc::ModuleInfo>(decoded);
        counts.requests = module.requests.size();
        counts.imports =
            module.imports.size() + module.namespace_imports.size();
        counts.exports = module.local_exports.size() +
                         module.indirect_exports.size() +
                         module.star_exports.size();
        WriteModuleStructure(text, module, out, declaration);
      } else
        out << "null";
      out << ",\"common_js\":";
      Flag(out, attributes.common_js);
      out << ",\"top_level_await\":";
      Flag(out, attributes.top_level_await);
    } else
      out << "null,\"module_status\":\"bundle_ownership_unresolved\"";
    out << ",\"record_fields\":[";
    bool comma = false;
    for (const auto& field :
         std::span(index.fields)
             .subspan(record.first_field, record.field_count)) {
      if (field.owner_offset != record.offset)
        throw StructureFailure{field.offset, StructureFault::kOwner};
      if (comma) out << ',';
      comma = true;
      out << "{\"entity\":" << field.offset.value << ",\"name\":";
      text.String(out, field.name_offset);
      out << ",\"type_index\":" << field.type_index.value
          << ",\"access_flags\":" << field.access_flags << ",\"value\":";
      if (field.value)
        out << "{\"encoding\":" << static_cast<unsigned>(field.value->encoding)
            << ",\"bits\":" << field.value->bits << '}';
      else
        out << "null";
      out << '}';
    }
    out << "],\"methods\":[";
    comma = false;
    for (const auto& method :
         std::span(index.methods)
             .subspan(record.first_method, record.method_count)) {
      if (method.owner_offset != record.offset)
        throw StructureFailure{method.offset, StructureFault::kOwner};
      if (comma) out << ',';
      comma = true;
      WriteMethod(text, index, lookup, method, out, declaration);
    }
    out << "],\"pending\":[\"class_layout\",\"globals\",\"local_scopes\","
           "\"source_signatures\",\"bodies\"]}\n";
    declaration << "export {};\n";
    if (quota.write_failed)
      return StructureFailure{record.offset, StructureFault::kWrite};
    if (quota.exceeded)
      return StructureFailure{record.offset, StructureFault::kOutputLimit};
    if (!out || !declaration)
      return StructureFailure{record.offset, StructureFault::kWrite};
    if (file.CheckUnchanged())
      return StructureFailure{record.offset, abc::MetadataError::kFileChanged};
    return counts;
  } catch (const StructureFailure& failure) {
    if (quota.write_failed)
      return StructureFailure{record.offset, StructureFault::kWrite};
    if (quota.exceeded)
      return StructureFailure{record.offset, StructureFault::kOutputLimit};
    return failure;
  } catch (const std::bad_alloc&) {
    if (quota.write_failed)
      return StructureFailure{record.offset, StructureFault::kWrite};
    return StructureFailure{record.offset, StructureFault::kResourceLimit};
  } catch (const std::ios_base::failure&) {
    return StructureFailure{record.offset, StructureFault::kWrite};
  }
}
}  // namespace abcd::structure
