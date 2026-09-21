#include "abcd/corpus/detect.h"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <zlib.h>

namespace {
using abcd::corpus::AbcFile;
using abcd::corpus::DetectAbc;
using abcd::corpus::Detection;
using abcd::corpus::EvidenceRule;
using abcd::corpus::SampleKind;

void Check(bool value) {
  if (!value) {
    std::cerr << "detect_test failed\n";
    std::abort();
  }
}

void Put32(std::vector<std::uint8_t>* bytes, std::size_t offset,
           std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    (*bytes)[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
  }
}

std::vector<std::uint8_t> Header(std::array<std::uint8_t, 4> version,
                                  std::size_t size = 72) {
  std::vector<std::uint8_t> bytes(size);
  const std::array<std::uint8_t, 8> magic = {'P','A','N','D','A',0,0,0};
  std::copy(magic.begin(), magic.end(), bytes.begin());
  std::copy(version.begin(), version.end(), bytes.begin() + 12);
  Put32(&bytes, 16, static_cast<std::uint32_t>(size));
  return bytes;
}

void Seal(std::vector<std::uint8_t>* bytes) {
  Put32(bytes, 16, static_cast<std::uint32_t>(bytes->size()));
  const auto checksum = adler32(1, bytes->data() + 12,
                                static_cast<uInt>(bytes->size() - 12));
  Put32(bytes, 8, checksum);
}

class Fixture {
 public:
  Fixture() {
    char pattern[] = "detect_test.XXXXXX";
    const int fd = mkstemp(pattern);
    Check(fd >= 0 && close(fd) == 0);
    path_ = pattern;
  }
  ~Fixture() { std::filesystem::remove(path_); }

  Detection Detect(std::vector<std::uint8_t> bytes, bool seal = true) {
    if (seal) Seal(&bytes);
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.close();
    Check(!out.fail());
    auto opened = AbcFile::Open(path_);
    Check(std::holds_alternative<std::unique_ptr<AbcFile>>(opened));
    auto file = std::move(std::get<std::unique_ptr<AbcFile>>(opened));
    auto fingerprint = file->Fingerprint();
    Check(std::holds_alternative<abcd::corpus::FileFingerprint>(fingerprint));
    auto detection = DetectAbc(*file,
        std::get<abcd::corpus::FileFingerprint>(fingerprint));
    Check(std::holds_alternative<Detection>(detection));
    return std::get<Detection>(detection);
  }
 private:
  std::filesystem::path path_;
};

std::vector<std::uint8_t> Legacy(std::vector<std::uint8_t> record) {
  auto bytes = Header({0, 0, 0, 6}, 80 + record.size());
  Put32(&bytes, 32, 1);
  Put32(&bytes, 36, 72);
  Put32(&bytes, 72, 80);
  std::copy(record.begin(), record.end(), bytes.begin() + 80);
  return bytes;
}

void ClassifiesHeaders() {
  Fixture fixture;
  auto dynamic = fixture.Detect(Header({9, 0, 0, 0}));
  Check(dynamic.kind == SampleKind::kDynamic &&
        dynamic.evidence.rule == EvidenceRule::kDynamicHeader);
  auto current = fixture.Detect(Header({0, 1, 0, 7}));
  Check(current.kind == SampleKind::kStatic &&
        current.evidence.rule == EvidenceRule::kStaticHeader);
  auto future = fixture.Detect(Header({25, 0, 0, 0}));
  Check(future.kind == SampleKind::kUnknown &&
        future.evidence.rule == EvidenceRule::kUnknownVersion);
  auto incompatible = fixture.Detect(Header({11, 0, 0, 0}));
  Check(incompatible.kind == SampleKind::kUnknown && incompatible.header_error);
  auto malformed = Header({9, 0, 0, 0});
  malformed[0] = 'X';
  Check(fixture.Detect(malformed).kind == SampleKind::kInvalid);
  auto checksum = Header({9, 0, 0, 0});
  Seal(&checksum);
  checksum[60] = 1;
  auto invalid = fixture.Detect(checksum, false);
  Check(invalid.kind == SampleKind::kInvalid &&
        invalid.evidence.rule == EvidenceRule::kChecksumMismatch);
}

void ProbesOnlyParsedClasses() {
  Fixture fixture;
  // StringItem length ("LFoo;" = five UTF-16 units), terminated name,
  // superclass ID, access/field/method counts, SOURCE_LANG and ETS value.
  const std::vector<std::uint8_t> valid =
      {11, 'L','F','o','o',';',0, 0,0,0,0, 1,0,0, 2,5};
  auto witness = fixture.Detect(Legacy(valid));
  Check(witness.kind == SampleKind::kStatic &&
        witness.evidence.rule == EvidenceRule::kEtsClass &&
        witness.evidence.offset == 94 && witness.evidence.observed == 5);
  auto many = Header({0, 0, 0, 6}, 72 + 257 * 4 + valid.size());
  Put32(&many, 32, 257);
  Put32(&many, 36, 72);
  Put32(&many, 72, 72 + 257 * 4);
  std::copy(valid.begin(), valid.end(), many.begin() + 72 + 257 * 4);
  Check(fixture.Detect(many).kind == SampleKind::kStatic);
  auto absent = valid;
  absent.back() = 4;
  Check(fixture.Detect(Legacy(absent)).kind == SampleKind::kUnknown);
  auto malformed = valid;
  malformed[0] = 0x80;
  Check(fixture.Detect(Legacy(malformed)).kind == SampleKind::kUnknown);
  malformed = valid;
  malformed[1] = 'X';
  Check(fixture.Detect(Legacy(malformed)).kind == SampleKind::kUnknown);
  malformed = valid;
  malformed[14] = 1;
  Check(fixture.Detect(Legacy(malformed)).kind == SampleKind::kUnknown);
  malformed = valid;
  malformed[14] = 1;  // INTERFACES with an impossible index count.
  malformed.push_back(0x7f);
  Check(fixture.Detect(Legacy(malformed)).kind == SampleKind::kUnknown);
  auto random = Header({0, 0, 0, 6}, 96);
  random[81] = 2;
  random[82] = 5;
  Check(fixture.Detect(random).kind == SampleKind::kUnknown);
  auto header_offset = Legacy(valid);
  Put32(&header_offset, 72, 0);
  Check(fixture.Detect(header_offset).kind == SampleKind::kUnknown);
  auto foreign = Legacy(valid);
  Put32(&foreign, 20, 80);
  Put32(&foreign, 24, static_cast<std::uint32_t>(valid.size()));
  Check(fixture.Detect(foreign).kind == SampleKind::kUnknown);
  auto limited = Header({0, 0, 0, 6}, 80);
  Put32(&limited, 32, 257);
  Put32(&limited, 36, 72);
  // Layout validator rejects an index outside the file, so use a real table.
  limited.resize(72 + 257 * 4);
  for (std::size_t i = 0; i < 257; ++i) Put32(&limited, 72 + i * 4, 72);
  Check(fixture.Detect(limited).evidence.rule == EvidenceRule::kProbeLimit);
  auto byte_limited = Header({0, 0, 0, 6}, 256 + 64 * 1024);
  Put32(&byte_limited, 32, 17);
  Put32(&byte_limited, 36, 72);
  for (std::size_t i = 0; i < 17; ++i) Put32(&byte_limited, 72 + i * 4, 256);
  Check(fixture.Detect(byte_limited).evidence.rule == EvidenceRule::kProbeLimit);
}
}  // namespace

int main() {
  ClassifiesHeaders();
  ProbesOnlyParsedClasses();
}
