#include "abcd/source/created_class.h"

#include <unistd.h>

#include <sstream>

#include "analysis/ssa_fixture.h"

int main(int argc, char** argv) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64, kCaller = 200, kTarget = 300,
                          kBuffer = 128, kCode = 400;
  std::vector<std::uint8_t> bytes(512);
  const auto word = [&](std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[offset + i] = value >> (8 * i);
  };
  word(kPool, kTarget);
  word(kPool + sizeof(kTarget), kBuffer);
  constexpr std::uint32_t kMemberBuffer = 160, kName = 480;
  word(kPool + 2 * sizeof(kTarget), kMemberBuffer);
  word(kMemberBuffer, 6);
  bytes[kMemberBuffer + 4] = 5;
  word(kMemberBuffer + 5, kName);
  bytes[kMemberBuffer + 9] = 2;
  word(kMemberBuffer + 10, 1);
  bytes[kMemberBuffer + 14] = 2;
  bytes[kName] = 3;
  bytes[kName + 1] = 'x';
  word(kBuffer, 2);
  bytes[kBuffer + sizeof(std::uint32_t)] =
      2;  // Integer zero: empty class buffer.
  const std::vector<std::uint8_t> code{0, 4, 3, 0, 0x60, 3, 0x64};
  std::copy(code.begin(), code.end(), bytes.begin() + kCode);
  char path[] = "/tmp/abcd-created-class-XXXXXX";
  const auto fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCode, {}, {3, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller};
  auto& method = index.methods[1];
  method.offset = {kTarget};
  method.name = "#scope#Recovered^1";
  method.name_utf16_length = method.name.size();
  method.access_flags = 1U << 8;
  method.code = abc::CodeInfo{{kCode}, {kCode + 4}, 0, 4, 3, 0};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto caller =
      test::Body({0x70, 0x61, 0, 0x35, 0, 0, 0, 1, 0, 1, 0, 0, 0x65});
  const auto heritage = std::get<std::vector<analysis::ClassHeritage>>(
      analysis::ProveClassHeritage(caller, 1))[0];
  std::ostringstream output;
  const auto result =
      source::WriteCreatedClass(file, header, index, lookup, {kCaller},
                                caller.instructions[2], heritage, output);
  test::Check(std::holds_alternative<std::size_t>(result));
  test::Check(std::get<std::size_t>(result) == output.str().size());
  test::Check(output.str().starts_with("(class Recovered {\nconstructor($p0)"));
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export const recovered = " << output.str() << ";\n";
  for (const auto kind :
       {analysis::HeritageKind::kUnknown, analysis::HeritageKind::kNull}) {
    std::ostringstream rejected;
    auto wrong = heritage;
    wrong.kind = kind;
    test::Check(std::get<source::CreatedClassError>(source::WriteCreatedClass(
                    file, header, index, lookup, {kCaller},
                    caller.instructions[2], wrong, rejected)) ==
                source::CreatedClassError::kHeritage);
    test::Check(rejected.str().empty());
  }
  const auto render = [&](const abc::BodyInstruction& site,
                          source::LocalLexicalBudget budget = {}) {
    std::ostringstream text;
    const auto result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, site, heritage, text, budget);
    if (!std::holds_alternative<std::size_t>(result))
      test::Check(text.str().empty());
    return result;
  };
  auto bad_site = caller.instructions[2];
  ++bad_site.offset.value;
  test::Check(std::get<source::CreatedClassError>(render(bad_site)) ==
              source::CreatedClassError::kHeritage);
  bad_site = caller.instructions[2];
  bad_site.decoded.operands[2].raw_bits = 2;
  test::Check(std::get<source::CreatedClassError>(render(bad_site)) ==
              source::CreatedClassError::kMembers);
  bad_site.decoded.operands[2].raw_bits = 3;
  test::Check(std::get<abc::MetadataError>(render(bad_site)) ==
              abc::MetadataError::kIndexRange);
  const auto saved_code = method.code;
  method.code.reset();
  test::Check(
      std::get<source::CreatedClassError>(render(caller.instructions[2])) ==
      source::CreatedClassError::kMissingCode);
  method.code = saved_code;
  method.name = "$acc";
  method.name_utf16_length = method.name.size();
  test::Check(std::get<source::FunctionEmitError>(render(
                  caller.instructions[2])) == source::FunctionEmitError::kName);
  method.name = "Recovered";
  method.name_utf16_length = method.name.size();
  // v7.0 class creation refines all known initial kinds to CLASS_CONSTRUCTOR
  // for absent heritage; the initial BASE_CONSTRUCTOR enum is not required.
  constexpr unsigned kAccessBits = 8, kSendableKindBit = 3;
  for (std::size_t kind = 0; kind < abc::kFunctionKindCount; ++kind) {
    method.access_flags = kind << kAccessBits;
    test::Check(
        std::holds_alternative<std::size_t>(render(caller.instructions[2])));
  }
  method.access_flags = 1U << (kAccessBits + kSendableKindBit);
  test::Check(
      std::get<source::FunctionEmitError>(render(caller.instructions[2])) ==
      source::FunctionEmitError::kSendable);
  method.access_flags = 1U << kAccessBits;
  for (const auto count : {output.str().size() - 1, output.str().size()}) {
    source::LocalLexicalBudget budget;
    budget.output.bytes = count;
    test::Check(std::holds_alternative<std::size_t>(
                    render(caller.instructions[2], budget)) ==
                (count == output.str().size()));
  }
  std::size_t low = 0, high = source::LocalLexicalBudget{}.bytes;
  const auto fits = [&](std::size_t bytes) {
    source::LocalLexicalBudget budget;
    budget.bytes = bytes;
    return std::holds_alternative<std::size_t>(
        render(caller.instructions[2], budget));
  };
  test::Check(!fits(low) && fits(high));
  while (high - low > 1) {
    const auto middle = low + (high - low) / 2;
    if (fits(middle))
      high = middle;
    else
      low = middle;
  }
  test::Check(fits(high) && !fits(high - 1));
  test::Check(unlink(path) == 0);
}
