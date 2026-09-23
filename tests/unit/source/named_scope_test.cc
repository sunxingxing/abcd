#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "abcd/abc/literal_array.h"
#include "abcd/source/local_lexical_body.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;

constexpr std::size_t kPoolTable = 64;
constexpr std::size_t kScopeLiteral = 512;
constexpr std::size_t kScopeName = 600;
constexpr std::size_t kScopeSlot = 527;
constexpr std::size_t kLargeScopeName = 8192;

void Word(std::vector<std::uint8_t>& bytes, std::size_t at,
          std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::vector<std::uint8_t> ScopeFile() {
  std::vector<std::uint8_t> bytes(1024);
  Word(bytes, kPoolTable + 2 * sizeof(std::uint32_t), kScopeLiteral);
  Word(bytes, kScopeLiteral, 6);  // Three tag/value entries.
  bytes[kScopeLiteral + 4] =
      static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  Word(bytes, kScopeLiteral + 5, 1);
  bytes[kScopeLiteral + 9] =
      static_cast<std::uint8_t>(abc::LiteralTag::kString);
  Word(bytes, kScopeLiteral + 10, kScopeName);
  bytes[kScopeLiteral + 14] =
      static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  Word(bytes, kScopeSlot, 0);
  bytes[kScopeName] = 3;
  bytes[kScopeName + 1] = 'x';
  return bytes;
}

std::vector<std::uint8_t> RepeatedNameFile(std::size_t pairs,
                                           std::size_t name_units) {
  std::vector<std::uint8_t> bytes(kLargeScopeName + name_units + 8);
  Word(bytes, kPoolTable + 2 * sizeof(std::uint32_t), kScopeLiteral);
  Word(bytes, kScopeLiteral, static_cast<std::uint32_t>(2 * (1 + 2 * pairs)));
  auto at = kScopeLiteral + sizeof(std::uint32_t);
  bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  Word(bytes, at, static_cast<std::uint32_t>(pairs));
  at += sizeof(std::uint32_t);
  for (std::size_t i = 0; i < pairs; ++i) {
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
    Word(bytes, at, kLargeScopeName);
    at += sizeof(std::uint32_t);
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
    Word(bytes, at, 0);
    at += sizeof(std::uint32_t);
  }
  test::Check(at < kLargeScopeName);
  std::uint32_t prefix = static_cast<std::uint32_t>((name_units << 1) | 1);
  at = kLargeScopeName;
  do {
    const auto chunk = static_cast<std::uint8_t>(prefix & 0x7f);
    prefix >>= 7;
    bytes[at++] = static_cast<std::uint8_t>(chunk | (prefix ? 0x80 : 0));
  } while (prefix);
  std::fill_n(bytes.begin() + at, name_units, 'x');
  return bytes;
}

struct Attempt {
  source::LocalLexicalResult result;
  std::string text;
};

Attempt Write(std::vector<std::uint8_t> bytes, bool constants = true,
              std::size_t pushes = 1, std::uint8_t slots = 1,
              std::uint64_t work = source::LocalLexicalBudget{}.work) {
  char path[] = "/tmp/abcd-named-scope-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  test::Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({200, 300, {}, {3, kPoolTable}, {}, {}});
  const source::MethodConstants pool{file, header, index, {210}, {}};
  std::vector<std::uint8_t> code;
  code.reserve(pushes * 4 + 7);
  for (std::size_t i = 0; i < pushes; ++i)
    code.insert(code.end(), {0xb6, slots, 2, 0});
  if (slots)
    code.insert(code.end(), {0x60, 0, 0x3d, 0, 0x3c, 0, 0x64});
  else
    code.insert(code.end(), {0x60, 0, 0x64});
  const auto body = test::Body(code);
  const std::array<std::string_view, 1> registers{"r0"};
  std::ostringstream output;
  source::LocalLexicalBudget budget;
  budget.work = work;
  auto result = source::WriteLocalLexicalBody(
      body, {"acc", registers}, output, budget, constants ? &pool : nullptr);
  test::Check(unlink(path) == 0);
  return {std::move(result), output.str()};
}
}  // namespace

int main() {
  using namespace abcd;
  const auto good = Write(ScopeFile());
  test::Check(std::holds_alternative<std::size_t>(good.result));
  test::Check(good.text ==
              "let $lex0_0;\nacc = r0;\n$lex0_0 = acc;\n"
              "acc = $lex0_0;\nreturn acc;\n");
  const auto missing = Write(ScopeFile(), false);
  test::Check(std::get<source::BodyEmitFailure>(missing.result).kind ==
              source::BodyEmitError::kMissingConstants);
  test::Check(missing.text.empty());
  auto malformed = ScopeFile();
  malformed[kScopeLiteral + 4] =
      static_cast<std::uint8_t>(abc::LiteralTag::kBoolean);
  const auto wrong_header = Write(malformed);
  test::Check(std::get<source::BodyEmitFailure>(wrong_header.result).kind ==
              source::BodyEmitError::kConstant);
  test::Check(wrong_header.text.empty());
  malformed = ScopeFile();
  Word(malformed, kScopeSlot, 1);
  const auto wrong_slot = Write(malformed);
  test::Check(std::get<source::BodyEmitFailure>(wrong_slot.result).kind ==
              source::BodyEmitError::kConstant);
  test::Check(wrong_slot.text.empty());
  malformed = ScopeFile();
  malformed[kScopeName + 1] = 0x80;
  const auto wrong_name = Write(malformed);
  test::Check(std::get<source::BodyEmitFailure>(wrong_name.result).kind ==
              source::BodyEmitError::kConstant);
  test::Check(wrong_name.text.empty());
  auto empty = ScopeFile();
  Word(empty, kScopeLiteral, 2);
  Word(empty, kScopeLiteral + 5, 0);
  test::Check(std::holds_alternative<std::size_t>(
      Write(std::move(empty), true, 1, 0).result));
  for (const std::size_t count : {200, 201, 400})
    test::Check(std::holds_alternative<std::size_t>(
        Write(ScopeFile(), true, count).result));
  constexpr std::size_t kLongNameUnits = 16384;
  for (const std::size_t pairs : {200, 201, 400})
    test::Check(std::holds_alternative<std::size_t>(
        Write(RepeatedNameFile(pairs, kLongNameUnits)).result));
  const auto repeated = RepeatedNameFile(201, kLongNameUnits);
  const auto limited = Write(repeated, true, 1, 1, 1000);
  test::Check(std::holds_alternative<source::BodyEmitFailure>(limited.result));
  test::Check(std::get<source::BodyEmitFailure>(limited.result).kind ==
              source::BodyEmitError::kWorkLimit);
  test::Check(limited.text.empty());
}
