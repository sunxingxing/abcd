#include "abcd/source/record_source.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <source_location>

#include "abcd/analysis/method_roles.h"

namespace {
void Check(bool condition, std::source_location at = std::source_location::current()) {
  if (!condition) {
    std::cerr << at.file_name() << ":" << at.line() << " check failed\n";
    std::abort();
  }
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  constexpr std::uint32_t kRecord = 100;
  constexpr std::uint32_t kEntry = 120;
  constexpr std::uint32_t kTarget = 200;
  constexpr std::uint32_t kPool = 300;
  constexpr std::uint32_t kEntryCode = 350;
  constexpr std::uint32_t kTargetCode = 400;
  constexpr std::uint32_t kConstructCode = 600;
  constexpr std::uint32_t kUnknownArgumentCode = 700;
  constexpr std::uint32_t kName = 500;
  constexpr std::size_t kFixtureBytes = 1024;
  constexpr std::uint32_t kTypes = 800;
  constexpr std::uint32_t kModule = 860;
  constexpr std::uint32_t kModuleName = 920;
  constexpr std::uint32_t kModuleField = 950;
  constexpr std::uint32_t kI32FieldEncoding = 5; // types.yaml: i32 code 7 minus u1 code 2.
  constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
  // Pinned encoded-count, request/import/namespace counts precede local exports.
  constexpr std::size_t kLocalExportCount = 4 * kWordBytes;
  constexpr std::size_t kCodeHeaderBytes = 4;
  constexpr std::uint8_t kEntryArguments = 3;
  constexpr std::uint8_t kTargetArguments = 4;
  constexpr std::uint8_t kEntryInstructionBytes = 8;
  constexpr std::uint8_t kTargetInstructionBytes = 3;
  constexpr std::uint8_t kConstructInstructionBytes = 14;
  constexpr std::uint8_t kFunctionLength = 1;
  constexpr std::uint8_t kEmpty = 0;
  // Pinned v7 ISA bytes; the generated decoder independently verifies them.
  constexpr std::uint8_t kDefineFunc = 0x33;
  constexpr std::uint8_t kStoreModule = 0x7c;
  constexpr std::uint8_t kReturnUndefined = 0x65;
  constexpr std::uint8_t kLoadRegister = 0x60;
  constexpr std::uint8_t kReturn = 0x64;
  constexpr std::uint8_t kAsciiFlag = 1;
  constexpr unsigned kStringLengthShift = 1;
  std::array<std::uint8_t, kFixtureBytes> bytes{};
  for (unsigned i = 0; i < sizeof(std::uint32_t); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (8 * i));
  constexpr std::array<std::uint8_t, 12> kEntryBody{
      kEmpty, kEntryArguments, kEntryInstructionBytes, kEmpty,
      kDefineFunc, kEmpty, kEmpty, kEmpty, kFunctionLength,
      kStoreModule, kEmpty, kReturnUndefined};
  constexpr std::array<std::uint8_t, 7> kTargetBody{
      kEmpty, kTargetArguments, kTargetInstructionBytes, kEmpty,
      kLoadRegister, kEntryArguments, kReturn};
  constexpr std::array<std::uint8_t, 18> kConstructBody{
      1, kEntryArguments, kConstructInstructionBytes, kEmpty,
      kDefineFunc, kEmpty, kEmpty, kEmpty, kFunctionLength,
      0x61, 0, 0x08, 0, 1, 0, kStoreModule, kEmpty,
      kReturnUndefined};
  std::copy(kEntryBody.begin(), kEntryBody.end(),
            bytes.begin() + kEntryCode);
  std::copy(kTargetBody.begin(), kTargetBody.end(),
            bytes.begin() + kTargetCode);
  std::copy(kConstructBody.begin(), kConstructBody.end(),
            bytes.begin() + kConstructCode);
  auto unknown_argument = kConstructBody;
  unknown_argument[0] = 2;
  unknown_argument[13] = 2;
  std::copy(unknown_argument.begin(), unknown_argument.end(),
            bytes.begin() + kUnknownArgumentCode);
  constexpr std::string_view kNameText = "identity";
  bytes[kName] = (kNameText.size() << kStringLengthShift) | kAsciiFlag;
  std::copy(kNameText.begin(), kNameText.end(), bytes.begin() + kName + 1);
  // Merged-module wire metadata, not a fabricated context: requests/imports/
  // namespace counts precede the local/export pair (pinned module format).
  const auto word = [&](std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i)
      bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  constexpr std::string_view kModuleNameText = "moduleRecordIdx";
  bytes[kModuleName] = (kModuleNameText.size() << kStringLengthShift) | kAsciiFlag;
  std::copy(kModuleNameText.begin(), kModuleNameText.end(), bytes.begin() + kModuleName + 1);
  word(kTypes, kI32FieldEncoding);
  word(kModule + kLocalExportCount, 1);
  word(kModule + kLocalExportCount + kWordBytes, kName);
  word(kModule + kLocalExportCount + 2 * kWordBytes, kName);
  char path[] = "/tmp/abcd-record-function-XXXXXX";
  const auto fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1301;
  header.file_size = bytes.size();
  header.version = {13, 0, 1, 0};
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({kRecord, bytes.size(), {1, kTypes}, {1, kPool}, {}, {}});
  abc::RecordInfo record;
  record.offset = {kRecord};
  record.method_count = 2;
  record.field_count = 1;
  index.fields.push_back({{kModuleField}, record.offset, {kModuleName}, {}, 0,
      abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule}});
  index.records.push_back(record);
  abc::MethodInfo entry;
  entry.offset = {kEntry};
  entry.owner_offset = record.offset;
  entry.name = "func_main_0";
  entry.name_utf16_length = entry.name.size();
  entry.code = abc::CodeInfo{{kEntryCode}, {kEntryCode + kCodeHeaderBytes},
                             0, kEntryArguments, kEntryInstructionBytes, 0};
  index.methods.push_back(entry);
  abc::MethodInfo target;
  target.offset = {kTarget};
  target.owner_offset = record.offset;
  target.name = "#scope#identity^1";
  target.name_utf16_length = target.name.size();
  constexpr std::uint32_t kFunctionKindShift = 8;
  target.access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor)
      << kFunctionKindShift;
  target.code = abc::CodeInfo{{kTargetCode}, {kTargetCode + kCodeHeaderBytes},
                              0, kTargetArguments,
                              kTargetInstructionBytes, 0};
  index.methods.push_back(target);
  // All published fixtures use the production value-tree record route. A
  // legacy writer pass cannot certify family expansion or route identity.
  const auto build = [&](const abc::Header& current_header,
                         const abc::MetadataIndex& current_index) {
    const auto lookup = std::get<abc::MethodLookup>(
        abc::MethodLookup::Build(current_index.methods));
    const auto roles = std::get<analysis::MethodRoleIndex>(
        analysis::MethodRoleIndex::Build(file, current_header, current_index, lookup));
    const auto functions = std::get<analysis::FunctionCreationSites>(
        analysis::FunctionCreationSites::Build(file, current_header, current_index, lookup));
    const auto context = std::get<source::RecordModuleContext>(
        source::ReadRecordModuleContext(file, current_header, current_index, record,
                                       abc::FilePackaging::kMerged));
    source::RouteInputIdentity identity;
    identity.profile = current_header.profile;
    identity.abc_version = current_header.version;
    identity.file_size = current_header.file_size;
    return source::BuildRecordSource(file, current_header, current_index, lookup, record,
        context, roles, {}, nullptr, source::RoutePolicy::kRequireValueTree,
        nullptr, nullptr, &identity, &functions);
  };
  const auto printed = [](const source::RecordSourceResult& result) {
    if (!std::holds_alternative<source::PrintedSource>(result)) {
      std::cerr << "record variant=" << result.index() << '\n';
      if (const auto* failure = std::get_if<source::RecordProgramFailure>(&result))
        std::cerr << "failure=" << static_cast<unsigned>(failure->kind)
                  << " pc=" << failure->pc.value << '\n';
    }
    Check(std::holds_alternative<source::PrintedSource>(result));
    const auto& text = std::get<source::PrintedSource>(result);
    Check(!text.incomplete);
    return text.text;
  };
  const auto source_text = printed(build(header, index));
  auto async_index = index;
  async_index.methods[1].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kAsync)
      << kFunctionKindShift;
  const auto async_text = printed(build(header, async_index));
  auto constructed_index = index;
  constructed_index.methods[0].code = abc::CodeInfo{
      {kConstructCode}, {kConstructCode + kCodeHeaderBytes}, 1,
      kEntryArguments, kConstructInstructionBytes, 0};
  const auto constructed_text = printed(build(header, constructed_index));
  auto historical_header = header;
  historical_header.profile = abc::ProfileId::kDynamic1206;
  historical_header.version = {12, 0, 6, 0};
  printed(build(historical_header, constructed_index));
  auto invalid_index = constructed_index;
  invalid_index.methods[0].code = abc::CodeInfo{
      {kUnknownArgumentCode}, {kUnknownArgumentCode + kCodeHeaderBytes}, 2,
      kEntryArguments, kConstructInstructionBytes, 0};
  // An uninitialized caller register is not silently omitted from new(...).
  Check(!std::holds_alternative<source::PrintedSource>(build(header, invalid_index)));
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << source_text;
  if (argc == 2 && std::string_view(argv[1]) == "--emit-async")
    std::cout << async_text;
  if (argc == 2 && std::string_view(argv[1]) == "--emit-constructed")
    std::cout << constructed_text;
  Check(unlink(path) == 0);
}
