#include "abcd/source/created_class.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "analysis/ssa_fixture.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64;
  constexpr std::uint32_t kBuffer = 128;
  constexpr std::uint32_t kCaller = 200;
  constexpr std::uint32_t kConstructor = 300;
  constexpr std::uint32_t kCode = 400;
  constexpr std::uint32_t kPositionalCode = 448;
  constexpr std::size_t kFileBytes = 512;
  constexpr std::size_t kCodeHeaderBytes = 4;
  constexpr std::uint8_t kLocalSlots = 6;
  constexpr std::uint8_t kArgumentSlots = 4;
  const std::vector<std::uint8_t> constructor{
      0x44, 0x60, 0x44, 0x71, 0x44, 0x82, 0x44, 0x93,
      0x60, 0, 0x61, 5,
      0xfb, 0x19, 5, 0x61, 5,
      0x60, 2, 0xfe, 7, 1,
      0x60, 5, 0x61, 2, 0x60, 2,
      0xfe, 7, 0, 0x64};
  const std::vector<std::uint8_t> positional_constructor{
      0x44, 0x60, 0x44, 0x71, 0x44, 0x82, 0x44, 0x93,
      0x44, 0xa4, 0x60, 0, 0x61, 5,
      0x32, 7, 2, 3, 0x61, 5,
      0x60, 2, 0xfe, 7, 1,
      0x60, 5, 0x61, 2, 0x60, 2,
      0xfe, 7, 0, 0x64};
  std::vector<std::uint8_t> bytes(kFileBytes);
  const auto word = [&](std::uint32_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  word(kPool, kConstructor);
  word(kPool + sizeof(std::uint32_t), kBuffer);
  word(kBuffer, 2);
  bytes[kBuffer + sizeof(std::uint32_t)] = 2;
  bytes[kCode] = kLocalSlots;
  bytes[kCode + 1] = kArgumentSlots;
  Check(constructor.size() < UINT8_MAX);
  bytes[kCode + 2] = static_cast<std::uint8_t>(constructor.size());
  std::copy(constructor.begin(), constructor.end(),
            bytes.begin() + kCode + kCodeHeaderBytes);
  bytes[kPositionalCode] = kLocalSlots;
  bytes[kPositionalCode + 1] = kArgumentSlots + 1;
  Check(positional_constructor.size() < UINT8_MAX);
  bytes[kPositionalCode + 2] =
      static_cast<std::uint8_t>(positional_constructor.size());
  std::copy(positional_constructor.begin(), positional_constructor.end(),
            bytes.begin() + kPositionalCode + kCodeHeaderBytes);
  char path[] = "/tmp/abcd-derived-class-XXXXXX";
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
  index.regions.push_back({kCaller, kFileBytes, {}, {2, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller};
  auto& method = index.methods[1];
  method.offset = {kConstructor};
  method.name = "#scope#Derived^1";
  method.name_utf16_length = method.name.size();
  method.access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << 8;
  method.code = abc::CodeInfo{{kCode}, {kCode + kCodeHeaderBytes},
                              kLocalSlots, kArgumentSlots,
                              static_cast<std::uint32_t>(constructor.size()),
                              0};
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto caller = test::Body({0x35, 0, 0, 0, 1, 0, 0, 0, 0});
  const analysis::ClassHeritage heritage{
      caller.instructions[0].offset, analysis::HeritageKind::kUnknown};
  std::ostringstream output;
  const auto result = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, caller.instructions[0],
      heritage, output, {}, {}, nullptr, nullptr, nullptr, nullptr, {},
      source::ImportedClassParent{0});
  Check(std::holds_alternative<std::size_t>(result));
  Check(output.str().find("class Derived extends $loaded0") !=
        std::string::npos);
  Check(output.str().find("constructor() { super(...arguments); }") !=
        std::string::npos);
  Check(std::get<std::size_t>(result) == output.str().size());
  std::ostringstream global_output;
  const auto global_result = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, caller.instructions[0],
      heritage, global_output, {}, {}, nullptr, nullptr, nullptr, nullptr,
      {}, source::GlobalClassParent{0});
  Check(std::holds_alternative<std::size_t>(global_result));
  Check(global_output.str().find("class Derived extends $global0") !=
        std::string::npos);
  auto historical = header;
  historical.profile = abc::ProfileId::kDynamic1206;
  std::ostringstream wrong_profile;
  const auto rejected = source::WriteCreatedClass(
      file, historical, index, lookup, {kCaller}, caller.instructions[0],
      heritage, wrong_profile, {}, {}, nullptr, nullptr, nullptr, nullptr,
      {}, source::ImportedClassParent{0});
  Check(std::get<source::CreatedClassError>(rejected) ==
        source::CreatedClassError::kHeritage);
  Check(wrong_profile.str().empty());
  std::ostringstream wrong_global_profile;
  const auto rejected_global = source::WriteCreatedClass(
      file, historical, index, lookup, {kCaller}, caller.instructions[0],
      heritage, wrong_global_profile, {}, {}, nullptr, nullptr, nullptr,
      nullptr, {}, source::GlobalClassParent{0});
  Check(std::get<source::CreatedClassError>(rejected_global) ==
        source::CreatedClassError::kHeritage);
  Check(wrong_global_profile.str().empty());
  source::LocalLexicalBudget limited;
  limited.output.bytes = output.str().size() - 1;
  std::ostringstream short_output;
  const auto exhausted = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, caller.instructions[0],
      heritage, short_output, limited, {}, nullptr, nullptr, nullptr, nullptr,
      {}, source::ImportedClassParent{0});
  Check(std::get<source::EmitError>(exhausted) ==
        source::EmitError::kOutputLimit);
  Check(short_output.str().empty());
  method.code = abc::CodeInfo{{kPositionalCode},
                              {kPositionalCode + kCodeHeaderBytes},
                              kLocalSlots, kArgumentSlots + 1,
                              static_cast<std::uint32_t>(
                                  positional_constructor.size()),
                              0};
  auto positional_site = caller.instructions[0];
  positional_site.decoded.operands[3].raw_bits = 2;
  std::ostringstream positional_output;
  const auto positional_class = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, positional_site,
      heritage, positional_output, {}, {}, nullptr, nullptr, nullptr,
      nullptr, {}, source::ImportedClassParent{0});
  Check(std::holds_alternative<std::size_t>(positional_class));
  Check(positional_output.str().find(
            "constructor($p0, $p1) { super($p0, $p1); }") !=
        std::string::npos);
  auto wrong_length_site = positional_site;
  wrong_length_site.decoded.operands[3].raw_bits = 1;
  std::ostringstream wrong_length;
  const auto mismatched = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, wrong_length_site,
      heritage, wrong_length, {}, {}, nullptr, nullptr, nullptr,
      nullptr, {}, source::ImportedClassParent{0});
  Check(std::get<source::FunctionEmitError>(mismatched) ==
        source::FunctionEmitError::kLength);
  Check(wrong_length.str().empty());
  source::LocalLexicalBudget positional_limit;
  positional_limit.output.bytes = positional_output.str().size() - 1;
  std::ostringstream short_positional;
  const auto positional_exhausted = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, positional_site,
      heritage, short_positional, positional_limit, {}, nullptr, nullptr,
      nullptr, nullptr, {}, source::ImportedClassParent{0});
  Check(std::get<source::EmitError>(positional_exhausted) ==
        source::EmitError::kOutputLimit);
  Check(short_positional.str().empty());
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "const $loaded0 = class Base { constructor(...values) {"
                 " this.values = values; } };\nexport const Derived = "
              << output.str() << ";\n"
              << "export const PositionalDerived = "
              << positional_output.str() << ";\n"
              << "const $global0 = class Base { constructor(...values) {"
                 " this.values = values; } };\n"
              << "export const GlobalDerived = " << global_output.str()
              << ";\n";
  Check(unlink(path) == 0);
}
