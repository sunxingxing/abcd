#include <unistd.h>

#include <cstdlib>
#include <sstream>

#include "abcd/corpus/abc_file.h"
#include "record_text.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  std::vector<std::uint8_t> bytes(abc::kDynamicHeaderSize);
  const auto string = [&](std::string_view value) {
    const abc::EntityOffset offset{static_cast<std::uint32_t>(bytes.size())};
    Check(value.size() < 64);
    bytes.push_back(static_cast<std::uint8_t>((value.size() << 1) | 1));
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
    return offset;
  };
  const auto request = string("pkg");
  const auto star = string("*");
  const auto a = string("a"), b = string("b");
  const auto x = string("x"), y = string("y"), z = string("z");
  const auto empty = string("");
  constexpr auto kIndexBytes = sizeof(std::uint32_t);
  bytes.resize((bytes.size() + kIndexBytes - 1) / kIndexBytes * kIndexBytes);
  const auto table_offset = static_cast<std::uint32_t>(bytes.size());
  const auto program_offset = table_offset + kIndexBytes;
  for (std::size_t i = 0; i < kIndexBytes; ++i)
    bytes.push_back(static_cast<std::uint8_t>(program_offset >> (8 * i)));
  bytes.push_back(0);  // END_SEQUENCE.
  const abc::EntityOffset debug_offset{
      static_cast<std::uint32_t>(bytes.size())};
  Check(a.value < 128);
  bytes.insert(bytes.end(), {1, 3, static_cast<std::uint8_t>(a.value),
                             static_cast<std::uint8_t>(empty.value), 0, 0, 0});
  const auto bad_string = static_cast<std::uint8_t>(bytes.size());
  Check(bytes.size() < 128);
  bytes.insert(bytes.end(), {1, 0xff, 0});
  std::vector<abc::DebugInfo> bad_scopes;
  for (const bool local : {false, true}) {
    abc::DebugInfo info;
    info.constant_pool = {static_cast<std::uint32_t>(bytes.size())};
    bytes.push_back(bad_string);
    if (local) bytes.push_back(static_cast<std::uint8_t>(a.value));
    info.constant_pool_bytes = local ? 2 : 1;
    info.line_program = {static_cast<std::uint32_t>(bytes.size())};
    if (local)
      bytes.insert(bytes.end(), {3, 0, 0});
    else
      bytes.insert(bytes.end(), {9, 0});
    bad_scopes.push_back(info);
  }
  char path[] = "/tmp/abcd-structure-XXXXXX";
  const auto fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  close(fd);
  auto opened = corpus::AbcFile::Open(path);
  auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  abc::DynamicLayout layout;
  layout.line_number_programs = {1, table_offset};
  header.layout = layout;
  structure::RecordText text(file, header, {});
  abc::MethodInfo method;
  method.debug_info = debug_offset;
  method.code = abc::CodeInfo{};
  std::ostringstream debug;
  structure::WriteDebugStructure(text, method, debug);
  Check(debug.str().find("\"parameter_names\":[\"a\",\"\",null]") !=
        std::string::npos);
  Check(debug.str().find("\"scopes\":{\"terminal_pc\":0,\"locals\":[]") !=
        std::string::npos);
  for (const auto& info : bad_scopes) {
    std::ostringstream failed_scope;
    structure::WriteScopeStructure(text, method, info, failed_scope);
    Check(failed_scope.str().starts_with(",\"scopes\":null,\"scope_error\":"));
  }
  abc::ModuleInfo module;
  module.requests = {request};
  module.imports = {{a, star, {0}}};
  module.indirect_exports = {{x, star, {0}}};
  module.local_exports = {{a, x}, {a, y}, {b, z}, {a, star}};
  std::ostringstream metadata, declarations;
  structure::WriteModuleStructure(text, module, metadata, declarations);
  const auto source = declarations.str();
  Check(source.find("import * as $binding0 from \"pkg\";") !=
        std::string::npos);
  Check(source.find("export { \"*\" as \"x\" } from \"pkg\";") !=
        std::string::npos);
  Check(source.find("export { $local0 as \"x\" };") != std::string::npos);
  Check(source.find("export { $local0 as \"y\" };") != std::string::npos);
  Check(source.find("export { $local1 as \"z\" };") != std::string::npos);
  Check(source.find("export { $local2 as \"*\" };") != std::string::npos);
  const auto expect_failure = [&](structure::StructureFault expected) {
    try {
      structure::WriteModuleStructure(text, module, metadata, declarations);
      Check(false);
    } catch (const structure::StructureFailure& failure) {
      Check(std::get<structure::StructureFault>(failure.error) == expected);
    }
  };
  text.budget.binding_bytes = 0;
  expect_failure(structure::StructureFault::kResourceLimit);
  text.budget = {};
  module.imports.push_back(module.imports.front());
  expect_failure(structure::StructureFault::kBindings);
  module.imports.pop_back();
  for (const std::size_t count : {200U, 201U, 8192U}) {
    module.local_exports.assign(count, {a, x});
    std::ostringstream json, declaration;
    structure::WriteModuleStructure(text, module, json, declaration);
    Check(declaration.str().find("declare let $local1") == std::string::npos);
  }
  abc::MetadataIndex index;
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  abc::RecordInfo record;
  record.offset = request;
  structure::StructureBudget budget;
  budget.output_bytes = 1;
  const auto limited = structure::WriteRecordStructure(
      file, header, index, lookup, record, abc::FilePackaging::kBundle,
      metadata, declarations, budget);
  Check(std::get<structure::StructureFault>(
            std::get<structure::StructureFailure>(limited).error) ==
        structure::StructureFault::kOutputLimit);
  // A real short write by a target buffer is an output fault (kWrite) and
  // takes precedence over a later structural fault of the same record; quota
  // exhaustion (above) never reaches the targets.
  // The target accepts `capacity` bytes, then writes short, so the writer
  // continues past the failure and reaches the owner check.
  struct Refusing : std::streambuf {
    std::size_t capacity = 0;
    std::streamsize xsputn(const char*, std::streamsize count) override {
      const auto accepted = std::min<std::size_t>(capacity, static_cast<std::size_t>(count));
      capacity -= accepted;
      return static_cast<std::streamsize>(accepted);
    }
    int_type overflow(int_type value) override {
      if (capacity == 0) return traits_type::eof();
      --capacity;
      return value;
    }
  } refusing;
  std::ostream refused(&refusing);
  abc::MetadataIndex foreign;
  foreign.methods.resize(1);
  foreign.methods[0].owner_offset = abc::EntityOffset{request.value + 1};
  abc::RecordInfo owner;
  owner.offset = request;
  owner.method_count = 1;
  const auto foreign_lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(foreign.methods));
  const auto fault = [&](std::ostream& json, std::ostream& declaration) {
    return std::get<structure::StructureFault>(
        std::get<structure::StructureFailure>(structure::WriteRecordStructure(
            file, header, foreign, foreign_lookup, owner,
            abc::FilePackaging::kBundle, json, declaration)).error);
  };
  std::ostringstream healthy_json, healthy_declarations;
  Check(fault(healthy_json, healthy_declarations) == structure::StructureFault::kOwner);
  // The healthy run's JSON ends in the owner failure; a target that fails
  // after its first bytes still reports the write fault.
  for (const std::size_t accepted : {std::size_t{0}, std::size_t{1}, healthy_json.str().size() - 1}) {
    refusing.capacity = accepted;
    refused.clear();
    std::ostringstream other_declarations;
    Check(fault(refused, other_declarations) == structure::StructureFault::kWrite);
  }
  // Keep ctime stable until all lazy readers and identity checks finish.
  unlink(path);
}
