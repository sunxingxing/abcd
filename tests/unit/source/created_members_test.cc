#include <unistd.h>

#include <sstream>

#include "abcd/analysis/method_roles.h"
#include "abcd/source/created_class.h"
#include "analysis/ssa_fixture.h"

namespace {
void Run(std::string_view name, std::uint32_t split, abcd::abc::LiteralTag tag,
         bool expected, bool emit = false) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64, kLiteral = 128, kCaller = 256,
                          kConstructor = 300, kMember = 350, kStatic = 360,
                          kCode = 400, kMemberCode = 430, kCallerCode = 460,
                          kName = 600;
  std::vector<std::uint8_t> bytes(1024);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kConstructor);
  word(kPool + 4, kLiteral);
  word(kLiteral, 14);  // Two string/method/affiliate triples and the split.
  for (const auto at : {kLiteral + 4, kLiteral + 17}) {
    bytes[at] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
    word(at + 1, kName);
    bytes[at + 5] = static_cast<std::uint8_t>(tag);
    word(at + 6, at == kLiteral + 4 ? kMember : kStatic);
    bytes[at + 10] =
        static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
    bytes[at + 11] = 1;
  }
  bytes[kLiteral + 30] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kLiteral + 31, split);
  bytes[kName] = name.size() * 2 + 1;  // MUTF-8 ASCII flag.
  std::copy(name.begin(), name.end(), bytes.begin() + kName + 1);
  const std::vector<std::uint8_t> ctor{0, 3, 1, 0, 0x65};
  const std::vector<std::uint8_t> member{0, 4, 3, 0, 0x60, 0, 0x64};
  const std::vector<std::uint8_t> caller{1, 0, 13, 0, 0x70, 0x61, 0, 0x35, 0,
                                         0, 0, 1,  0, 0,    0,    0, 0x65};
  std::copy(ctor.begin(), ctor.end(), bytes.begin() + kCode);
  std::copy(member.begin(), member.end(), bytes.begin() + kMemberCode);
  std::copy(caller.begin(), caller.end(), bytes.begin() + kCallerCode);
  constexpr std::uint32_t kBadCode = 700;
  const std::vector<std::uint8_t> unsupported{0, 4, 2, 0, 0x70, 0x64};
  std::copy(unsupported.begin(), unsupported.end(), bytes.begin() + kBadCode);
  char path[] = "/tmp/abcd-created-members-XXXXXX";
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
  index.regions.push_back({kCaller, kCode, {}, {2, kPool}, {}, {}});
  index.methods.resize(4);
  index.methods[0].offset = {kCaller};
  index.methods[0].code =
      abc::CodeInfo{{kCallerCode}, {kCallerCode + 4}, 1, 0, 13, 0};
  index.methods[1].offset = {kConstructor};
  index.methods[1].name = "MethodSelf0";
  index.methods[1].name_utf16_length = index.methods[1].name.size();
  index.methods[1].code = abc::CodeInfo{{kCode}, {kCode + 4}, 0, 3, 1, 0};
  auto& method = index.methods[2];
  method.offset = {kMember};
  method.name = "#scope#" + std::string(name) + "^1";
  method.name_utf16_length = method.name.size();
  method.code = abc::CodeInfo{{kMemberCode}, {kMemberCode + 4}, 0, 4, 3, 0};
  index.methods[3] = method;
  index.methods[3].offset = {kStatic};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  test::Check(roles.At({2}) == (tag == abc::LiteralTag::kMethod
                                    ? analysis::MethodRole::kOrdinary
                                    : analysis::MethodRole::kOther));
  const auto body =
      test::Body({0x70, 0x61, 0, 0x35, 0, 0, 0, 1, 0, 0, 0, 0, 0x65});
  const auto heritage = std::get<std::vector<analysis::ClassHeritage>>(
      analysis::ProveClassHeritage(body, 1))[0];
  std::ostringstream output;
  const auto result = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, body.instructions[2], heritage,
      output, {}, {}, &roles);
  test::Check(std::holds_alternative<std::size_t>(result) == expected);
  if (!expected) {
    test::Check(output.str().empty());
    test::Check(unlink(path) == 0);
    return;
  }
  test::Check(std::get<std::size_t>(result) == output.str().size());
  if (emit) std::cout << "export const recovered = " << output.str() << ";\n";
  const auto render = [&](source::LocalLexicalBudget budget = {},
                          const analysis::MethodRoleIndex* proof = nullptr) {
    std::ostringstream text;
    const auto result = source::WriteCreatedClass(
        file, header, index, lookup, {kCaller}, body.instructions[2], heritage,
        text, budget, {}, proof);
    if (!std::holds_alternative<std::size_t>(result))
      test::Check(text.str().empty());
    return result;
  };
  test::Check(std::get<source::CreatedClassError>(render()) ==
              source::CreatedClassError::kMembers);
  const auto saved_name = method.name;
  method.name = "wrong";
  method.name_utf16_length = method.name.size();
  test::Check(std::get<source::CreatedClassError>(render({}, &roles)) ==
              source::CreatedClassError::kName);
  method.name = saved_name;
  method.name_utf16_length = method.name.size();
  constexpr unsigned kKindShift = 8;
  method.access_flags = static_cast<unsigned>(abc::FunctionKind::kAsync)
                        << kKindShift;
  test::Check(std::get<source::CreatedClassError>(render({}, &roles)) ==
              source::CreatedClassError::kMembers);
  method.access_flags = 0;
  const auto saved_code = method.code;
  method.code.reset();
  test::Check(std::get<source::CreatedClassError>(render({}, &roles)) ==
              source::CreatedClassError::kMissingCode);
  method.code = saved_code;
  index.methods[3].code = abc::CodeInfo{{kBadCode}, {kBadCode + 4}, 0, 4, 2, 0};
  // Last member fails, after a valid constructor and first member in dry run.
  test::Check(!std::holds_alternative<std::size_t>(render({}, &roles)));
  index.methods[3].code = saved_code;
  for (const auto count : {output.str().size() - 1, output.str().size()}) {
    source::LocalLexicalBudget budget;
    budget.output.bytes = count;
    test::Check(std::holds_alternative<std::size_t>(render(budget, &roles)) ==
                (count == output.str().size()));
  }
  std::size_t low = 0, high = source::LocalLexicalBudget{}.bytes;
  const auto fits = [&](std::size_t bytes) {
    source::LocalLexicalBudget budget;
    budget.bytes = bytes;
    return std::holds_alternative<std::size_t>(render(budget, &roles));
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
}  // namespace
int main(int argc, char** argv) {
  using abcd::abc::LiteralTag;
  Run("method", 1, LiteralTag::kMethod, true,
      argc == 2 && std::string_view(argv[1]) == "--emit");
  Run("quoted\"key", 1, LiteralTag::kMethod, true);
  Run("__proto__", 1, LiteralTag::kMethod, true);
  Run("", 1, LiteralTag::kMethod, true);
  for (const auto key :
       {"constructor", "name", "length", "prototype", "0", "4294967294"})
    Run(key, 1, LiteralTag::kMethod, false);
  Run("4294967295", 1, LiteralTag::kMethod, true);
  Run("method", 0, LiteralTag::kMethod, false);
  Run("method", 2, LiteralTag::kMethod, false);
  for (const auto tag :
       {LiteralTag::kGetter, LiteralTag::kSetter, LiteralTag::kGeneratorMethod,
        LiteralTag::kAsyncGeneratorMethod})
    Run("method", 1, tag, false);
}
