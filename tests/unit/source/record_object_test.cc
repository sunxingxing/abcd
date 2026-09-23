#include "abcd/source/record_program.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

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
  constexpr std::uint32_t kPool = 300;
  constexpr std::uint32_t kEntryCode = 400;
  constexpr std::uint32_t kAnswer = 500;
  constexpr std::uint32_t kPrototype = 520;
  constexpr std::uint32_t kText = 540;
  constexpr std::uint32_t kConfig = 560;
  constexpr std::uint32_t kAlias = 580;
  constexpr std::uint32_t kBufferLiteral = 640;
  constexpr std::uint32_t kId = 680;
  constexpr std::uint32_t kParams = 700;
  constexpr std::uint32_t kRoutes = 720;
  constexpr std::size_t kFileBytes = 1024;
  constexpr std::uint32_t kCodeHeaderBytes = 4;
  constexpr std::uint8_t kEntryArguments = 3;
  constexpr std::uint8_t kLocalSlots = 4;
  constexpr std::uint8_t kIntegerValue = 42;
  std::vector<std::uint8_t> instructions{
      0x04, 0x61, 0,
      0x62, kIntegerValue, 0, 0, 0,
      0xdc, 0, 0, 0, 0,
      0x3e, 2, 0,
      0xdb, 0, 1, 0, 0,
      0x3e, 2, 0, 0x61, 3,
      0x62, kIntegerValue, 0, 0, 0,
      0xfb, 1, 0, 3, 0,
      0x62, 7, 0, 0, 0,
      0xfb, 2, 0, 5, 0, 0, 0, 0,
      0x60, 0, 0x7c, 0,
      0x7c, 1,
      0x05, 0, 0x61, 1,
      0x07, 0, 3, 0, 0x61, 2,
      0x05, 0, 0x61, 3,
      0x60, 3, 0xdc, 0, 4, 0, 2,
      0x60, 2, 0x79, 0, 1, 0, 0,
      0x60, 1, 0x7c, 2, 0x65};
  std::array<std::uint8_t, kFileBytes> bytes{};
  const auto word = [&](std::uint32_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  word(kPool, kAnswer);
  word(kPool + sizeof(std::uint32_t), kPrototype);
  word(kPool + 2 * sizeof(std::uint32_t), kText);
  word(kPool + 3 * sizeof(std::uint32_t), kBufferLiteral);
  word(kPool + 4 * sizeof(std::uint32_t), kParams);
  const auto string = [&](std::uint32_t at, std::string_view value) {
    bytes[at] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at + 1);
  };
  string(kAnswer, "answer");
  string(kPrototype, "__proto__");
  string(kText, "plain");
  string(kConfig, "config");
  string(kAlias, "alias");
  string(kId, "id");
  string(kParams, "params");
  string(kRoutes, "routes");
  word(kBufferLiteral, 4);
  bytes[kBufferLiteral + 4] =
      static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(kBufferLiteral + 5, kId);
  bytes[kBufferLiteral + 9] =
      static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kBufferLiteral + 10, kIntegerValue);
  bytes[kEntryCode] = kLocalSlots;
  bytes[kEntryCode + 1] = kEntryArguments;
  Check(instructions.size() < UINT8_MAX);
  bytes[kEntryCode + 2] = instructions.size();
  std::copy(instructions.begin(), instructions.end(),
            bytes.begin() + kEntryCode + kCodeHeaderBytes);
  char path[] = "/tmp/abcd-record-object-XXXXXX";
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
  header.profile = abc::ProfileId::kDynamic1301;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({kRecord, kFileBytes, {}, {5, kPool}, {}, {}});
  abc::RecordInfo record;
  record.offset = {kRecord};
  record.method_count = 1;
  index.records.push_back(record);
  abc::MethodInfo entry;
  entry.offset = {kEntry};
  entry.owner_offset = record.offset;
  entry.name = "func_main_0";
  entry.name_utf16_length = entry.name.size();
  entry.code = abc::CodeInfo{{kEntryCode}, {kEntryCode + kCodeHeaderBytes},
                             kLocalSlots, kEntryArguments,
                             static_cast<std::uint32_t>(instructions.size()),
                             0};
  index.methods.push_back(entry);
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  abc::ModuleInfo module;
  module.local_exports.push_back({{kConfig}, {kConfig}});
  module.local_exports.push_back({{kAlias}, {kAlias}});
  module.local_exports.push_back({{kRoutes}, {kRoutes}});
  std::ostringstream output;
  const auto result = source::WriteRecordProgram(
      file, header, index, lookup, record, module, roles, output);
  if (const auto* failure = std::get_if<source::RecordProgramFailure>(&result)) {
    std::cerr << "source_error=" << static_cast<unsigned>(failure->kind)
              << " pc=" << failure->pc.value << '\n';
    return EXIT_FAILURE;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << output.str() << std::flush;
  Check(output.str().find("[\"answer\"]: (42)") != std::string::npos);
  Check(output.str().find("[\"__proto__\"]: \"plain\"") !=
        std::string::npos);
  Check(output.str().find("[\"plain\"]: (42)") != std::string::npos);
  Check(output.str().find("[\"5\"]: (7)") != std::string::npos);
  Check(output.str().find("[\"params\"]: []") != std::string::npos);
  Check(std::get<std::size_t>(result) == output.str().size());
  source::RecordProgramBudget limited;
  limited.output.bytes = output.str().size() - 1;
  std::ostringstream rejected;
  const auto refusal = source::WriteRecordProgram(
      file, header, index, lookup, record, module, roles, rejected, limited);
  Check(std::get<source::RecordProgramFailure>(refusal).kind ==
        source::RecordProgramError::kOutputLimit);
  Check(rejected.str().empty());
  Check(!file.CheckUnchanged());
  Check(unlink(path) == 0);
}
