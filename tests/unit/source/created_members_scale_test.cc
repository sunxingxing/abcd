#include <unistd.h>

#include <sstream>

#include "abcd/source/created_class.h"
#include "analysis/ssa_fixture.h"

namespace {
void Run(std::uint32_t count, bool emit) {
  using namespace abcd;
  constexpr std::uint32_t kPool = 64, kCaller = 200, kConstructor = 300,
                          kFirstMethod = 400, kMemberBytes = 13,
                          kStringBytes = 16, kTrailingSplitBytes = 5,
                          kCountBytes = sizeof(std::uint32_t);
  const auto literal = kFirstMethod + count;
  const auto names =
      literal + kCountBytes + count * kMemberBytes + kTrailingSplitBytes;
  const auto code = names + count * kStringBytes;
  const auto member_code = code + 16, caller_code = code + 32;
  std::vector<std::uint8_t> bytes(code + 64);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kConstructor);
  word(kPool + sizeof(std::uint32_t), literal);
  constexpr std::uint32_t kEntriesPerMethod = 3, kCountUnits = 2;
  word(literal, (count * kEntriesPerMethod + 1) * kCountUnits);
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, literal, {}, {2, kPool}, {}, {}});
  index.methods.resize(count + 2);
  index.methods[0].offset = {kCaller};
  index.methods[0].code =
      abc::CodeInfo{{caller_code}, {caller_code + 4}, 1, 0, 13, 0};
  index.methods[1].offset = {kConstructor};
  index.methods[1].name = "ClassValue";
  index.methods[1].name_utf16_length = index.methods[1].name.size();
  index.methods[1].code = abc::CodeInfo{{code}, {code + 4}, 0, 3, 1, 0};
  for (std::uint32_t i = 0; i < count; ++i) {
    auto& method = index.methods[i + 2];
    method.offset = {kFirstMethod + i};
    method.name = "m" + std::to_string(i);
    method.name_utf16_length = method.name.size();
    method.code = abc::CodeInfo{{member_code}, {member_code + 4}, 0, 3, 3, 0};
    const auto at = literal + sizeof(std::uint32_t) + i * kMemberBytes;
    const auto key = names + i * kStringBytes;
    bytes[at] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
    word(at + 1, key);
    bytes[at + 5] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod);
    word(at + 6, method.offset.value);
    bytes[at + 10] =
        static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
    test::Check(method.name.size() + 2 <= kStringBytes);
    bytes[key] = method.name.size() * 2 + 1;
    std::copy(method.name.begin(), method.name.end(), bytes.begin() + key + 1);
  }
  const auto split = literal + sizeof(std::uint32_t) + count * kMemberBytes;
  bytes[split] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(split + 1, count);
  const std::vector<std::uint8_t> ctor{0, 3, 1, 0, 0x65};
  const std::vector<std::uint8_t> member{0, 3, 3, 0, 0x60, 0, 0x64};
  const std::vector<std::uint8_t> caller{1, 0, 13, 0, 0x70, 0x61, 0, 0x35, 0,
                                         0, 0, 1,  0, 0,    0,    0, 0x65};
  std::copy(ctor.begin(), ctor.end(), bytes.begin() + code);
  std::copy(member.begin(), member.end(), bytes.begin() + member_code);
  std::copy(caller.begin(), caller.end(), bytes.begin() + caller_code);
  char path[] = "/tmp/abcd-created-members-scale-XXXXXX";
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
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto body =
      test::Body({0x70, 0x61, 0, 0x35, 0, 0, 0, 1, 0, 0, 0, 0, 0x65});
  const auto heritage = std::get<std::vector<analysis::ClassHeritage>>(
      analysis::ProveClassHeritage(body, 1))[0];
  std::ostringstream output;
  const auto result = source::WriteCreatedClass(
      file, header, index, lookup, {kCaller}, body.instructions[2], heritage,
      output, {}, {}, &roles);
  test::Check(std::holds_alternative<std::size_t>(result));
  test::Check(std::get<std::size_t>(result) == output.str().size());
  if (emit)
    std::cout << "export const members" << count << " = " << output.str()
              << ";\n";
  test::Check(unlink(path) == 0);
}
}  // namespace
int main(int argc, char** argv) {
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  // Pinned MAX_FAST_PROPS_CAPACITY=1023 includes prototype.constructor:
  // 1022/1023 ordinary methods straddle fast/dictionary layout admission.
  for (const std::uint32_t count : {200, 201, 400, 800, 1022, 1023, 1600})
    Run(count, emit);
}
