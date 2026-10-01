#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>
#include <iostream>

#include "abcd/corpus/abc_file.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_source.h"
#include "abcd/analysis/method_roles.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
void Word(std::vector<std::uint8_t>& bytes, std::size_t offset,
          std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void String(std::vector<std::uint8_t>& bytes, std::size_t offset,
            std::string_view value) {
  Check(value.size() < 64);
  bytes[offset++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
  std::copy(value.begin(), value.end(), bytes.begin() + offset);
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  constexpr std::uint32_t kTypeTable = 200;
  constexpr std::uint32_t kRecord = 500;
  constexpr std::uint32_t kModule = 420;
  constexpr std::uint32_t kRequest = 560;
  std::vector<std::uint8_t> bytes(640);
  String(bytes, abc::kDynamicHeaderSize, "moduleRecordIdx");
  String(bytes, 90, "isCommonjs");
  const std::string_view mode = argc > 1 ? argv[1] : "";
  const bool stored_branch = mode.starts_with("stored-branch");
  const bool anonymous = mode.starts_with("anonymous");
  const bool named_default = mode == "named-default";
  String(bytes, kRequest, anonymous || named_default ? "default" : "answer");
  if (anonymous || named_default) String(bytes, 130, anonymous ? "*default*" : "answer");
  bytes[kTypeTable] = 5;
  // Empty requests/imports/namespaces, then one local/export name pair.
  Word(bytes, kModule + 16, 1);
  Word(bytes, kModule + 20, anonymous || named_default ? 130 : kRequest);
  Word(bytes, kModule + 24, kRequest);
  const bool array = mode == "anonymous-array" || (mode == "source" && argc > 2);
  const bool late = mode == "anonymous-late" || (mode == "source" && argc > 3);
  const bool repeated = mode == "anonymous-update";
  std::vector<std::uint8_t> body{0x62, 42, 0, 0, 0, 0x7c, 0, 0x65};
  std::uint32_t registers = 0;
  if (array) {
    // Snapshot the RHS before allocation; late RHS remains a separate gate.
    body = {0x62, 42, 0, 0, 0, 0x61, 1, 0x05, 0, 0x61, 0,
        0x60, 1, 0x79, 0, 0, 0, 0, 0x60, 0, 0x7c, 0, 0x65};
    registers = 2;
  }
  if (late) {
    body = {0x05, 0, 0x61, 0, 0x62, 42, 0, 0, 0,
        0x79, 0, 0, 0, 0, 0x60, 0, 0x7c, 0, 0x65};
    registers = 1;
  }
  if (repeated) body = {0x62, 42, 0, 0, 0, 0x7c, 0,
      0x62, 7, 0, 0, 0, 0x7c, 0, 0x65};
  if (stored_branch) {
    const std::uint8_t initial = mode == "stored-branch-zero" ? 0 : 1;
    // Real local read and two conditional updates of an already declared slot.
    body = {0x62, initial, 0, 0, 0, 0x7c, 0, 0x7d, 0, 0x4f, 11,
        0x62, 7, 0, 0, 0, 0x7c, 0, 0x4d, 9,
        0x62, 9, 0, 0, 0, 0x7c, 0, 0x65};
  }
  if (mode == "conditional-first" || mode == "branch-before-root") {
    body = {0x62, 1, 0, 0, 0, 0x4f, 11,
        0x62, 7, 0, 0, 0, 0x7c, 0, 0x4d, 9,
        0x62, 9, 0, 0, 0, 0x7c, 0};
    if (mode == "branch-before-root") body.insert(body.end(), {0x62, 42, 0, 0, 0, 0x7c, 0});
    body.push_back(0x65);
  }
  bytes[596] = static_cast<std::uint8_t>(registers);
  bytes[597] = 3; bytes[598] = static_cast<std::uint8_t>(body.size());
  std::copy(body.begin(), body.end(), bytes.begin() + 600);
  char path[] = "/tmp/abcd-record-module-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = argc > 2 && std::string_view(argv[2]) == "1202" ? abc::ProfileId::kDynamic1202 :
      argc > 2 && std::string_view(argv[2]) == "1301" ? abc::ProfileId::kDynamic1301 : abc::ProfileId::kDynamic1206;
  header.version = header.profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12, 0, 2, 0} :
      header.profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13, 0, 1, 0} : abc::AbcVersion{12, 0, 6, 0};
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({250, 600, {1, kTypeTable}, {}, {}, {}});
  abc::RecordInfo record;
  record.offset = {kRecord};
  record.field_count = 1;
  index.fields.push_back({{300}, record.offset,
                          {abc::kDynamicHeaderSize}, {}, 0,
                          abc::FieldValue{abc::FieldValueEncoding::kSignedLeb,
                                          kModule}});
  auto read = [&] {
    return source::ReadRecordModuleContext(
        file, header, index, record, abc::FilePackaging::kMerged);
  };
  record.method_count = 1;
  abc::MethodInfo entry;
  entry.offset = {320}; entry.owner_offset = record.offset;
  entry.name = "func_main_0"; entry.name_utf16_length = entry.name.size();
  entry.code = abc::CodeInfo{{596}, {600}, registers, 3, static_cast<std::uint32_t>(body.size()), 0};
  index.methods.push_back(entry); index.records.push_back(record);
  const auto context = std::get<source::RecordModuleContext>(read());
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  source::RouteInputIdentity identity;
  identity.profile = header.profile; identity.abc_version = header.version; identity.file_size = header.file_size;
  auto result = source::BuildRecordSource(file, header, index, lookup, record, context, roles,
      {}, nullptr, source::RoutePolicy::kRequireValueTree, nullptr, nullptr, &identity);
  if (const auto* error = std::get_if<source::RecordProgramFailure>(&result)) {
    if (mode == "conditional-first" || mode == "branch-before-root")
      Check(error->kind == source::RecordProgramError::kInitializer);
    Check(unlink(path) == 0);
    std::cerr << "record error " << static_cast<unsigned>(error->kind) << '\n'; return 1;
  }
  if (!std::holds_alternative<source::PrintedSource>(result)) {
    std::cerr << "variant " << result.index() << '\n'; return 1;
  }
  const auto& source = std::get<source::PrintedSource>(result).text;
  if (stored_branch) {
    Check(source.find(mode == "stored-branch-zero" ? "let answer = 0;" : "let answer = 1;") != std::string::npos);
    Check(source.find("if (") != std::string::npos);
    Check(source.find("(answer = 7)") != std::string::npos && source.find("(answer = 9)") != std::string::npos);
  } else if (anonymous) {
    Check(source.find("*default*") == std::string::npos);
    if (repeated) {
      Check(source.find("export default") == std::string::npos);
      Check(source.find(" as \"default\" }") != std::string::npos);
    } else {
      Check(source.find(array || late ? "export default [42];" : "export default 42;") != std::string::npos);
      Check(source.find("let $default") == std::string::npos);
    }
  } else if (named_default) {
    Check(source.find("let answer = 42;") != std::string::npos);
    Check(source.find("export { answer as \"default\" }") != std::string::npos);
  } else {
    Check(source.find(array ? "[" : "let answer = 42;") != std::string::npos);
    Check(source.find("export { answer as ") != std::string::npos);
  }
  if (argc > 1) std::cout << source;
  auto invalid_identity = identity; ++invalid_identity.file_size;
  Check(!std::holds_alternative<source::PrintedSource>(source::BuildRecordSource(
      file, header, index, lookup, record, context, roles, {}, nullptr,
      source::RoutePolicy::kRequireValueTree, nullptr, nullptr, &invalid_identity)));
  source::RecordProgramBudget small; small.output.bytes = 1;
  Check(!std::holds_alternative<source::PrintedSource>(source::BuildRecordSource(
      file, header, index, lookup, record, context, roles, small, nullptr,
      source::RoutePolicy::kRequireValueTree, nullptr, nullptr, &identity)));
  Check(unlink(path) == 0);
}
