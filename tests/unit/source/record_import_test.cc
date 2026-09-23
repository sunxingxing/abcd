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
  constexpr std::uint32_t kConstructor = 200;
  constexpr std::uint32_t kPool = 300;
  constexpr std::uint32_t kCode = 400;
  constexpr std::uint32_t kRequest = 500;
  constexpr std::uint32_t kImported = 540;
  constexpr std::uint32_t kExported = 560;
  constexpr std::uint32_t kBaseName = 580;
  constexpr std::uint32_t kDerivedName = 600;
  constexpr std::uint32_t kConstructorCode = 650;
  constexpr std::uint32_t kClassBuffer = 700;
  constexpr std::size_t kFileBytes = 1024;
  constexpr std::size_t kCodeHeaderBytes = 4;
  constexpr std::uint8_t kEntryArguments = 3;
  constexpr std::uint8_t kLocalSlots = 1;
  constexpr std::uint8_t kExternalLoad = 0x7e;
  constexpr std::uint8_t kGuardPrefix = 0xfe;
  constexpr std::uint8_t kGuardNamed = 9;
  constexpr std::uint8_t kStoreRegister = 0x61;
  constexpr std::uint8_t kLoadInteger = 0x62;
  constexpr std::uint8_t kLoadRegister = 0x60;
  constexpr std::uint8_t kStoreModule = 0x7c;
  constexpr std::uint8_t kReturnUndefined = 0x65;
  constexpr std::uint8_t kOverwrittenAccumulator = 42;
  // Pinned v7 wire bytes: external import load, adjacent Hole guard, snapshot
  // into v0, unrelated accumulator write, then export the saved value.
  const std::vector<std::uint8_t> instructions{
      kExternalLoad, 0, kGuardPrefix, kGuardNamed, 0, 0,
      kStoreRegister, 0, kLoadInteger, kOverwrittenAccumulator, 0, 0, 0,
      kLoadRegister, 0, kStoreModule, 0,
      kExternalLoad, 1, kGuardPrefix, kGuardNamed, 1, 0,
      kStoreRegister, 0,
      0x35, 0, 2, 0, 3, 0, 0, 0, 0,
      kStoreRegister, 0,
      kExternalLoad, 0, kGuardPrefix, kGuardNamed, 0, 0,
      kLoadRegister, 0, kStoreModule, 1, kReturnUndefined};
  const std::vector<std::uint8_t> constructor{
      0x44, 0x60, 0x44, 0x71, 0x44, 0x82, 0x44, 0x93,
      0x60, 0, 0x61, 5,
      0xfb, 0x19, 5, 0x61, 5,
      0x60, 2, 0xfe, 7, 1,
      0x60, 5, 0x61, 2, 0x60, 2,
      0xfe, 7, 0, 0x64};
  std::array<std::uint8_t, kFileBytes> bytes{};
  for (std::size_t i = 0; i < sizeof(std::uint32_t); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kImported >> (8 * i));
  const auto pool = [&](std::size_t slot, std::uint32_t entity) {
    for (std::size_t i = 0; i < sizeof(entity); ++i)
      bytes[kPool + slot * sizeof(entity) + i] =
          static_cast<std::uint8_t>(entity >> (8 * i));
  };
  pool(1, kBaseName);
  pool(2, kConstructor);
  pool(3, kClassBuffer);
  const auto string = [&](std::uint32_t at, std::string_view value) {
    Check(value.size() < 64);
    bytes[at] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at + 1);
  };
  string(kRequest, "./dependency.mjs");
  string(kImported, "value");
  string(kExported, "copied");
  string(kBaseName, "Base");
  string(kDerivedName, "Derived");
  pool(3, kClassBuffer);
  bytes[kClassBuffer] = 2;
  bytes[kClassBuffer + sizeof(std::uint32_t)] = 2;
  bytes[kCode] = kLocalSlots;
  bytes[kCode + 1] = kEntryArguments;
  Check(instructions.size() < UINT8_MAX);
  bytes[kCode + 2] = static_cast<std::uint8_t>(instructions.size());
  std::copy(instructions.begin(), instructions.end(),
            bytes.begin() + kCode + kCodeHeaderBytes);
  bytes[kConstructorCode] = 6;
  bytes[kConstructorCode + 1] = 4;
  bytes[kConstructorCode + 2] = static_cast<std::uint8_t>(constructor.size());
  std::copy(constructor.begin(), constructor.end(),
            bytes.begin() + kConstructorCode + kCodeHeaderBytes);
  char path[] = "/tmp/abcd-record-import-XXXXXX";
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
  index.regions.push_back({kRecord, kFileBytes, {}, {4, kPool}, {}, {}});
  abc::RecordInfo record;
  record.offset = {kRecord};
  record.method_count = 2;
  index.records.push_back(record);
  abc::MethodInfo entry;
  entry.offset = {kEntry};
  entry.owner_offset = record.offset;
  entry.name = "func_main_0";
  entry.name_utf16_length = entry.name.size();
  entry.code = abc::CodeInfo{{kCode}, {kCode + kCodeHeaderBytes},
                             kLocalSlots, kEntryArguments,
                             static_cast<std::uint32_t>(instructions.size()),
                             0};
  index.methods.push_back(entry);
  abc::MethodInfo derived;
  derived.offset = {kConstructor};
  derived.owner_offset = record.offset;
  derived.name = "#scope#Derived^1";
  derived.name_utf16_length = derived.name.size();
  derived.access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << 8;
  derived.code = abc::CodeInfo{{kConstructorCode},
                               {kConstructorCode + kCodeHeaderBytes}, 6, 4,
                               static_cast<std::uint32_t>(constructor.size()),
                               0};
  index.methods.push_back(derived);
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  abc::ModuleInfo module;
  module.requests.push_back({kRequest});
  module.imports.push_back({{kImported}, {kImported}, {0}});
  module.imports.push_back({{kBaseName}, {kBaseName}, {0}});
  module.local_exports.push_back({{kExported}, {kExported}});
  module.local_exports.push_back({{kDerivedName}, {kDerivedName}});
  std::ostringstream output;
  const auto result = source::WriteRecordProgram(
      file, header, index, lookup, record, module, roles, output);
  if (const auto* failure = std::get_if<source::RecordProgramFailure>(&result)) {
    std::cerr << "source_error=" << static_cast<unsigned>(failure->kind)
              << " pc=" << failure->pc.value << '\n';
    return EXIT_FAILURE;
  }
  Check(output.str().find("const $loaded0 = $import0;") !=
        std::string::npos);
  Check(output.str().find("$acc = $loaded0;") != std::string::npos);
  Check(output.str().find("class Derived extends $loaded1") !=
        std::string::npos);
  Check(output.str().find("const $class0") <
        output.str().find("const $loaded2"));
  Check(std::get<std::size_t>(result) == output.str().size());
  auto missing_import = module;
  missing_import.imports.clear();
  std::ostringstream rejected;
  const auto refusal = source::WriteRecordProgram(
      file, header, index, lookup, record, missing_import, roles, rejected);
  Check(std::get<source::RecordProgramFailure>(refusal).kind ==
        source::RecordProgramError::kModule);
  Check(rejected.str().empty());
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << output.str();
  Check(unlink(path) == 0);
}
