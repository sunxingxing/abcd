#include "abcd/source/record_program.h"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

#include "abcd/analysis/method_roles.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
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
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({kRecord, bytes.size(), {}, {1, kPool}, {}, {}});
  abc::RecordInfo record;
  record.offset = {kRecord};
  record.method_count = 2;
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
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  abc::ModuleInfo module;
  module.local_exports.push_back({{kName}, {kName}});
  std::ostringstream source_text;
  const auto result = source::WriteRecordProgram(
      file, header, index, lookup, record, module, roles, source_text);
  if (const auto* failure =
          std::get_if<source::RecordProgramFailure>(&result)) {
    std::cerr << "failure=" << static_cast<unsigned>(failure->kind)
              << " pc=" << failure->pc.value << '\n';
    return EXIT_FAILURE;
  }
  Check(source_text.str().find("function identity($p0)") !=
        std::string::npos);
  Check(source_text.str().find("export { $local0 as \"identity\" };") !=
        std::string::npos);
  Check(std::get<std::size_t>(result) == source_text.str().size());
  auto async_index = index;
  async_index.methods[1].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kAsync)
      << kFunctionKindShift;
  const auto async_lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(async_index.methods));
  const auto async_roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, async_index,
                                       async_lookup));
  std::ostringstream async_text;
  const auto async_result = source::WriteRecordProgram(
      file, header, async_index, async_lookup, record, module, async_roles,
      async_text);
  if (const auto* failure =
          std::get_if<source::RecordProgramFailure>(&async_result))
    std::cerr << "async_failure=" << static_cast<unsigned>(failure->kind)
              << " pc=" << failure->pc.value << '\n';
  Check(std::holds_alternative<std::size_t>(async_result));
  Check(async_text.str().find("async function identity($p0)") !=
        std::string::npos);
  auto constructed_index = index;
  constructed_index.methods[0].code = abc::CodeInfo{
      {kConstructCode}, {kConstructCode + kCodeHeaderBytes}, 1,
      kEntryArguments, kConstructInstructionBytes, 0};
  const auto constructed_lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(constructed_index.methods));
  const auto constructed_roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, constructed_index,
                                       constructed_lookup));
  std::ostringstream constructed_text;
  const auto constructed_result = source::WriteRecordProgram(
      file, header, constructed_index, constructed_lookup, record, module,
      constructed_roles, constructed_text);
  Check(std::holds_alternative<std::size_t>(constructed_result));
  Check(constructed_text.str().find("new ($function0)()") !=
        std::string::npos);
  Check(constructed_text.str().find("$acc = $instance0;") !=
        std::string::npos);
  auto historical_header = header;
  historical_header.profile = abc::ProfileId::kDynamic1206;
  const auto historical_roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, historical_header,
                                       constructed_index, constructed_lookup));
  std::ostringstream historical_text;
  Check(std::holds_alternative<std::size_t>(source::WriteRecordProgram(
      file, historical_header, constructed_index, constructed_lookup,
      record, module, historical_roles, historical_text)));
  Check(historical_text.str().find("new ($function0)()") !=
        std::string::npos);
  const auto reject_construct = [&](std::uint32_t offset,
                                    source::RecordProgramError expected) {
    auto invalid_index = constructed_index;
    invalid_index.methods[0].code->offset = {offset};
    invalid_index.methods[0].code->instruction_offset = {
        offset + static_cast<std::uint32_t>(kCodeHeaderBytes)};
    invalid_index.methods[0].code->registers =
        offset == kUnknownArgumentCode ? 2 : 1;
    const auto invalid_lookup = std::get<abc::MethodLookup>(
        abc::MethodLookup::Build(invalid_index.methods));
    const auto invalid_roles = std::get<analysis::MethodRoleIndex>(
        analysis::MethodRoleIndex::Build(file, header, invalid_index,
                                         invalid_lookup));
    std::ostringstream rejected;
    const auto result = source::WriteRecordProgram(
        file, header, invalid_index, invalid_lookup, record, module,
        invalid_roles, rejected);
    Check(std::get<source::RecordProgramFailure>(result).kind == expected);
    Check(rejected.str().empty());
  };
  reject_construct(kUnknownArgumentCode,
                   source::RecordProgramError::kValue);
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << source_text.str();
  if (argc == 2 && std::string_view(argv[1]) == "--emit-async")
    std::cout << async_text.str();
  if (argc == 2 && std::string_view(argv[1]) == "--emit-constructed")
    std::cout << constructed_text.str();
  Check(unlink(path) == 0);
}
