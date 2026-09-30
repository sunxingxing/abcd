#include "abcd/analysis/method_roles.h"

#include <unistd.h>

#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"

namespace {
void Scale(std::uint32_t count, abcd::abc::LiteralTag tag = abcd::abc::LiteralTag::kMethod) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64, kCaller = 80, kTarget = 100,
                          kLiteral = 128;
  constexpr std::uint32_t kPairBytes = 8, kEntriesPerPair = 2, kCountUnits = 2;
  const auto code = kLiteral + sizeof(std::uint32_t) + count * kPairBytes;
  std::vector<std::uint8_t> bytes(code);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kLiteral);
  word(kLiteral, count * kEntriesPerPair * kCountUnits);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto at = kLiteral + sizeof(std::uint32_t) + i * kPairBytes;
    bytes[at] = static_cast<std::uint8_t>(tag);
    word(at + 1, kTarget);
    bytes[at + 5] =
        static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
  }
  const auto uleb = [&](std::uint32_t value) {
    do {
      const auto part = value & 0x7f;
      value >>= 7;
      bytes.push_back(part | (value ? 0x80 : 0));
    } while (value);
  };
  uleb(0);
  uleb(0);
  uleb(count * 4 + 1);
  uleb(0);
  const auto instructions = bytes.size();
  for (std::uint32_t i = 0; i < count; ++i)
    bytes.insert(bytes.end(), {7, 0, 0, 0});
  bytes.push_back(0x65);
  char path[] = "/tmp/abcd-method-role-scale-XXXXXX";
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
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kLiteral, {}, {1, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller};
  index.methods[0].code =
      abc::CodeInfo{{static_cast<std::uint32_t>(code)},
                    {static_cast<std::uint32_t>(instructions)},
                    0,
                    0,
                    count * 4 + 1,
                    0};
  index.methods[1].offset = {kTarget};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  test::Check(roles.At({1}) == (tag == abc::LiteralTag::kGeneratorMethod ? analysis::MethodRole::kGenerator : analysis::MethodRole::kOrdinary));
  test::Check(unlink(path) == 0);
}
}  // namespace

int main() {
  using namespace abcd;
  for (const std::uint32_t count : {200, 201, 400, 800, 1600}) Scale(count);
  for (const std::uint32_t count : {200, 201, 402}) Scale(count, abc::LiteralTag::kGeneratorMethod);
  constexpr std::uint32_t kCaller = 200, kTarget = 240, kPool = 64,
                          kOrdinary = 128, kGenerator = 160, kCode = 300;
  std::vector<std::uint8_t> bytes(512);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kOrdinary);
  word(kPool + 4, kGenerator);
  word(kPool + 8, kTarget);
  for (const auto offset : {kOrdinary, kGenerator}) {
    word(offset, 4);
    bytes[offset + 4] = offset == kOrdinary ? 6 : 7;
    word(offset + 5, kTarget);
    bytes[offset + 9] = 9;
  }
  // Object and array buffers exercise both MethodLiteral mutation paths.
  const std::vector<std::uint8_t> code{0, 0, 9, 0, 7, 0,   0,
                                       0, 6, 0, 1, 0, 0x65};
  std::copy(code.begin(), code.end(), bytes.begin() + kCode);
  constexpr std::uint32_t kOneCode = 400, kOtherCode = 420, kLegacyCode = 440;
  const std::vector<std::uint8_t> one{0, 0, 9, 0, 7, 0, 0, 0, 7, 0, 0, 0, 0x65};
  const std::vector<std::uint8_t> other{0, 0, 5, 0, 7, 0, 1, 0, 0x65};
  const std::vector<std::uint8_t> legacy{0, 0, 5, 0, 0xfc, 3, 0, 0, 0x65};
  std::copy(one.begin(), one.end(), bytes.begin() + kOneCode);
  std::copy(other.begin(), other.end(), bytes.begin() + kOtherCode);
  std::copy(legacy.begin(), legacy.end(), bytes.begin() + kLegacyCode);
  constexpr std::uint32_t kConstructorCode = 460;
  const std::vector<std::uint8_t> constructor{1, 0, 10, 0, 0x35, 0, 2,
                                              0, 0, 0,  0, 0,    0, 0x65};
  std::copy(constructor.begin(), constructor.end(),
            bytes.begin() + kConstructorCode);
  char path[] = "/tmp/abcd-method-roles-XXXXXX";
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
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCode, {}, {3, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller};
  index.methods[0].code = abc::CodeInfo{{kCode}, {kCode + 4}, 0, 0, 9, 0};
  index.methods[1].offset = {kTarget};
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto built =
      analysis::MethodRoleIndex::Build(file, header, index, lookup);
  const auto& roles = std::get<analysis::MethodRoleIndex>(built);
  test::Check(roles.At({0}) == analysis::MethodRole::kUnseen);
  test::Check(roles.At({1}) == analysis::MethodRole::kConflict);
  test::Check(!roles.At({2}));
  test::Check(std::holds_alternative<analysis::MethodRoleFailure>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup, {0})));
  auto movable = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  auto moved = std::move(movable);
  test::Check(!movable.At({0}) && movable.AllocatedBytes() == 0);
  test::Check(moved.At({1}) == analysis::MethodRole::kConflict);
  movable = std::move(moved);
  test::Check(!moved.At({0}) && moved.AllocatedBytes() == 0);
  test::Check(movable.At({1}) == analysis::MethodRole::kConflict);
  index.methods[0].code = abc::CodeInfo{{kOneCode}, {kOneCode + 4}, 0, 0, 9, 0};
  auto ordinary = analysis::MethodRoleIndex::Build(file, header, index, lookup);
  test::Check(std::get<analysis::MethodRoleIndex>(ordinary).At({1}) ==
              analysis::MethodRole::kOrdinary);
  index.methods[0].code =
      abc::CodeInfo{{kConstructorCode}, {kConstructorCode + 4}, 1, 0, 10, 0};
  auto constructor_roles =
      analysis::MethodRoleIndex::Build(file, header, index, lookup);
  test::Check(std::get<analysis::MethodRoleIndex>(constructor_roles).At({1}) ==
              analysis::MethodRole::kConflict);
  index.methods[0].code =
      abc::CodeInfo{{kOtherCode}, {kOtherCode + 4}, 0, 0, 5, 0};
  auto nonordinary =
      analysis::MethodRoleIndex::Build(file, header, index, lookup);
  test::Check(std::get<analysis::MethodRoleIndex>(nonordinary).At({1}) ==
              analysis::MethodRole::kGenerator);
  index.methods[0].code =
      abc::CodeInfo{{kLegacyCode}, {kLegacyCode + 4}, 0, 0, 5, 0};
  const auto refused =
      analysis::MethodRoleIndex::Build(file, header, index, lookup);
  test::Check(std::get<analysis::MethodRoleError>(
                  std::get<analysis::MethodRoleFailure>(refused).error) ==
              analysis::MethodRoleError::kLegacyLiteral);
  index.methods[0].code = abc::CodeInfo{{kCode}, {kCode + 4}, 0, 0, 9, 0};
  const auto missing = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(std::span(index.methods).first(1)));
  const auto bad =
      analysis::MethodRoleIndex::Build(file, header, index, missing);
  test::Check(std::get<abc::MethodTargetError>(
                  std::get<analysis::MethodRoleFailure>(bad).error) ==
              abc::MethodTargetError::kTargetMissing);
  std::size_t low = 0, high = analysis::MethodRoleBudget{}.bytes;
  const auto fits = [&](std::size_t bytes) {
    return std::holds_alternative<analysis::MethodRoleIndex>(
        analysis::MethodRoleIndex::Build(file, header, index, lookup, {bytes}));
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
  header.kind = abc::AbcKind::kStatic;
  const auto static_result =
      analysis::MethodRoleIndex::Build(file, header, index, lookup);
  test::Check(std::get<abc::MetadataError>(
                  std::get<analysis::MethodRoleFailure>(static_result).error) ==
              abc::MetadataError::kUnsupportedProfile);
  test::Check(unlink(path) == 0);
}
