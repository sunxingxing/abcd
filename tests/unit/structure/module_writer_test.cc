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
  char path[] = "/tmp/abcd-structure-XXXXXX";
  const auto fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  close(fd);
  auto opened = corpus::AbcFile::Open(path);
  auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  unlink(path);
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
  std::ostringstream debug;
  structure::WriteDebugStructure(text, method, debug);
  Check(debug.str().find("\"parameter_names\":[\"a\",\"\",null]") !=
        std::string::npos);
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
  abc::RecordInfo record;
  record.offset = request;
  structure::StructureBudget budget;
  budget.output_bytes = 1;
  const auto limited = structure::WriteRecordStructure(
      file, header, index, record, abc::FilePackaging::kBundle, metadata,
      declarations, budget);
  Check(std::get<structure::StructureFault>(
            std::get<structure::StructureFailure>(limited).error) ==
        structure::StructureFault::kOutputLimit);
}
