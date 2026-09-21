#include "abcd/abc/call_type.h"

#include <unistd.h>

#include <cstdlib>
#include <string_view>
#include <vector>

#include "abcd/abc/frame_layout.h"
#include "abcd/corpus/abc_file.h"

namespace {
using namespace abcd::abc;
void Check(bool condition) {
  if (!condition) std::abort();
}
struct Fixture {
  std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(1024);
  Header header;
  MetadataIndex index;
  MethodAnnotations annotations{{64}, 1};
  void Word(std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = value >> (8 * i);
  }
  void String(std::size_t at, std::string_view value) {
    Check(value.size() < 64);
    bytes[at++] = (value.size() << 1) | 1;
    for (char ch : value) bytes[at++] = ch;
    bytes[at] = 0;
  }
  Fixture() {
    header.kind = AbcKind::kDynamic;
    header.profile = ProfileId::kDynamic1206;
    header.layout = DynamicLayout{};
    header.file_size = bytes.size();
    // The annotation belongs to this region; the method tag does not.
    index.regions.push_back({200, 400, {1, 128}, {}, {}, {}});
    Word(128, 512);
    String(512, "L_ESCallTypeAnnotation;");
    String(560, "callType");
    bytes[64] = 6;
    Word(65, 256);
    bytes[258] = 2;  // class index 0, two elements.
    Word(260, 560);
    Word(264, 1);
    Word(268, 560);
    Word(272, 8);  // Duplicate element must not override the first.
    bytes[276] = 'I';
    bytes[277] = 'I';
  }
  auto Read(MetadataBudget budget = {}) {
    char path[] = "/tmp/abcd-call-type-XXXXXX";
    const int fd = mkstemp(path);
    Check(fd >= 0);
    Check(write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size()));
    close(fd);
    auto opened = abcd::corpus::AbcFile::Open(path);
    Check(
        std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
    auto result =
        ReadCallType(*std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened),
                     header, index, annotations, budget);
    unlink(path);
    return result;
  }
};
}  // namespace

int main() {
  using namespace abcd::abc;
  Fixture basic;
  Check(std::get<CallType>(basic.Read()).value == 1);
  basic.annotations.count = 2;
  basic.bytes[69] = 6;
  basic.Word(70, 300);
  basic.bytes[302] = 1;
  basic.Word(304, 560);
  basic.Word(308, 8);
  basic.bytes[312] = 'I';
  Check(std::get<CallType>(basic.Read()).value == 8);
  const auto flags = DecodeCallFlags(std::get<CallType>(basic.Read()).value);
  const auto frame = std::get<FrameLayout>(BuildFrameLayout(2, 4, flags));
  Check(frame.function->value == 2 && !frame.this_value && !frame.new_target &&
        frame.first_parameter.value == 3 && frame.parameters == 3);
  basic.bytes[302] = 0;  // Later annotation with no element does not clear.
  Check(std::get<CallType>(basic.Read()).value == 1);
  basic.annotations.count = 0;
  basic.annotations.first_tag.value = UINT32_MAX;
  Check(!std::get<CallType>(basic.Read()).value);

  Fixture no_match;
  no_match.String(512, "L_Unrelated;");
  Check(!std::get<CallType>(no_match.Read()).value);
  no_match = Fixture{};
  no_match.String(560, "other");
  Check(!std::get<CallType>(no_match.Read()).value);

  Fixture bad;
  bad.bytes[64] = 3;
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kUnknownTag);
  bad = Fixture{};
  bad.bytes[256] = 1;
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kIndexRange);
  bad = Fixture{};
  bad.index.regions.clear();
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kRegionRange);
  bad = Fixture{};
  bad.Word(260, UINT32_MAX);
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kReference);
  bad = Fixture{};
  bad.bytes[258] = 255;
  bad.bytes[259] = 255;
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kTruncated);
  bad = Fixture{};
  bad.header.profile = ProfileId::kUnknown;
  Check(std::get<MetadataError>(bad.Read()) ==
        MetadataError::kUnsupportedProfile);
  bad = Fixture{};
  Check(std::get<MetadataError>(bad.Read({0})) ==
        MetadataError::kResourceLimit);
  bad = Fixture{};
  bad.annotations.count = UINT32_MAX;
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kReference);
  bad = Fixture{};
  bad.Word(65, 1020);
  bad.bytes[1022] = 1;
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kTruncated);
  bad = Fixture{};
  bad.header.file_size -= 1;
  Check(std::get<MetadataError>(bad.Read()) == MetadataError::kFileChanged);

  for (const std::uint32_t count : {200u, 201u, 4096u}) {
    Fixture many;
    constexpr std::uint32_t kTagStart = 700;
    constexpr std::uint32_t kTaggedReferenceBytes = 5;
    many.bytes.resize(kTagStart + count * kTaggedReferenceBytes);
    many.header.file_size = many.bytes.size();
    many.annotations = {{kTagStart}, count};
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto at = kTagStart + i * kTaggedReferenceBytes;
      many.bytes[at] = 6;
      many.Word(at + 1, 256);
    }
    Check(std::get<CallType>(many.Read()).value == 1);
  }

  for (const auto profile :
       {ProfileId::kDynamic9, ProfileId::kDynamic1102, ProfileId::kDynamic1202,
        ProfileId::kDynamic1206, ProfileId::kDynamic1301,
        ProfileId::kDynamic2400}) {
    Fixture version;
    version.header.profile = profile;
    Check(std::get<CallType>(version.Read()).value == 1);
  }
}
