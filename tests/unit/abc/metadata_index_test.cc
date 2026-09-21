#include "abcd/abc/metadata_index.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace {

void Check(bool condition) {
  if (!condition) std::abort();
}

void Put32(std::vector<std::uint8_t>& bytes, std::size_t at,
           std::uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes[at + i] = (value >> (i * 8)) & 0xff;
}

void Put16(std::vector<std::uint8_t>& bytes, std::size_t at,
           std::uint16_t value) {
  bytes[at] = value & 0xff;
  bytes[at + 1] = value >> 8;
}

struct Fixture {
  std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(512, 0);
  abcd::abc::Header header;

  Fixture() {
    header.kind = abcd::abc::AbcKind::kDynamic;
    header.profile = abcd::abc::ProfileId::kDynamic1206;
    header.file_size = bytes.size();
    header.foreign_offset = 480;
    header.foreign_size = 32;
    header.layout = abcd::abc::DynamicLayout{{1, 64}, {0, 0}, {0, 0}, {1, 72}};
    Put32(bytes, 64, 128);
    Put32(bytes, 72, 120);
    Put32(bytes, 76, 480);
    Put32(bytes, 80, 1);
    Put32(bytes, 84, 112);
    Put32(bytes, 88, 0);
    Put32(bytes, 92, 0);
    Put32(bytes, 96, 0);
    Put32(bytes, 100, 0);
    Put32(bytes, 104, 1);
    Put32(bytes, 108, 116);
    Put32(bytes, 112, 128);
    Put32(bytes, 116, 300);
    // Class: Lfoo;, one field and one method.
    bytes[128] = 11;  // (UTF-16 length 5 << 1) | ASCII.
    bytes[129] = 'L';
    bytes[130] = 'f';
    bytes[131] = 'o';
    bytes[132] = 'o';
    bytes[133] = ';';
    bytes[134] = 0;
    Put32(bytes, 135, 0);
    bytes[139] = 1;
    bytes[140] = 1;
    bytes[141] = 1;
    bytes[142] = 0;
    // Field: signed LEB -1, followed by two annotations.
    Put16(bytes, 143, 0);
    Put16(bytes, 145, 0);
    Put32(bytes, 147, 320);
    bytes[151] = 1;
    bytes[152] = 1;
    bytes[153] = 0x7f;
    bytes[154] = 3;
    Put32(bytes, 155, 350);
    bytes[159] = 3;
    Put32(bytes, 160, 354);
    bytes[164] = 0;
    // Method with regional owner and proto resolution, plus code prefix.
    Put16(bytes, 165, 0);
    Put16(bytes, 167, 0);
    Put32(bytes, 169, 330);
    bytes[173] = 1;
    bytes[174] = 1;
    Put32(bytes, 175, 360);
    bytes[179] = 3;
    Put32(bytes, 180, 370);
    bytes[184] = 0;
    bytes[320] = 3;
    bytes[321] = 'f';
    bytes[322] = 0;
    bytes[330] = 3;
    bytes[331] = 'm';
    bytes[332] = 0;
    bytes[360] = 2;
    bytes[361] = 1;
    bytes[362] = 3;
    bytes[363] = 0;
    bytes[364] = 0xaa;
    bytes[365] = 0xbb;
    bytes[366] = 0xcc;
  }

  std::variant<abcd::abc::MetadataIndex, abcd::abc::MetadataError> Build(
      std::size_t budget = 1024 * 1024) const {
    char path[] = "/tmp/abcd-metadata-XXXXXX";
    const int fd = mkstemp(path);
    Check(fd >= 0);
    Check(write(fd, bytes.data(), bytes.size()) ==
          static_cast<ssize_t>(bytes.size()));
    close(fd);
    auto opened = abcd::corpus::AbcFile::Open(path);
    Check(
        std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
    auto result = abcd::abc::BuildMetadataIndex(
        *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened), header,
        {budget});
    unlink(path);
    return result;
  }
};

}  // namespace

int main() {
  Fixture fixture;
  auto result = fixture.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  const auto& index = std::get<abcd::abc::MetadataIndex>(result);
  Check(index.records.size() == 1);
  Check(index.records[0].descriptor == "Lfoo;");
  Check(index.records[0].field_count == 1);
  Check(index.records[0].method_count == 1);
  Check(index.methods.size() == 1);
  Check(index.methods[0].name == "m");
  Check(index.methods[0].owner_offset.value == 128);
  Check(index.methods[0].proto_offset->value == 300);
  Check(index.methods[0].code.has_value());
  Check(index.methods[0].code->offset.value == 360);
  Check(index.methods[0].code->instruction_offset.value == 364);
  Check(index.methods[0].code->instruction_bytes == 3);

  Fixture bad_owner;
  Put32(bad_owner.bytes, 112, 129);
  Check(std::get<abcd::abc::MetadataError>(bad_owner.Build()) ==
        abcd::abc::MetadataError::kOwnerMismatch);

  Fixture truncated_name;
  Put32(truncated_name.bytes, 169, 511);
  truncated_name.bytes[511] = 0x80;
  Check(std::get<abcd::abc::MetadataError>(truncated_name.Build()) ==
        abcd::abc::MetadataError::kTruncated);

  Fixture overlapping;
  overlapping.header.layout =
      abcd::abc::DynamicLayout{{1, 64}, {0, 0}, {0, 0}, {2, 400}};
  for (std::size_t i = 0; i < 40; ++i) {
    overlapping.bytes[400 + i] = overlapping.bytes[72 + i];
  }
  Put32(overlapping.bytes, 440, 400);
  Put32(overlapping.bytes, 444, 500);
  Check(std::get<abcd::abc::MetadataError>(overlapping.Build()) ==
        abcd::abc::MetadataError::kRegionOverlap);

  Fixture malformed_sleb;
  for (std::size_t i = 153; i < 157; ++i) {
    malformed_sleb.bytes[i] = 0xff;
  }
  malformed_sleb.bytes[157] = 0x08;
  Check(std::get<abcd::abc::MetadataError>(malformed_sleb.Build()) ==
        abcd::abc::MetadataError::kMalformedLeb);

  Fixture partial_absence;
  Put32(partial_absence.bytes, 96, UINT32_MAX);
  Check(std::get<abcd::abc::MetadataError>(partial_absence.Build()) ==
        abcd::abc::MetadataError::kIndexRange);
  partial_absence = Fixture();
  Put32(partial_absence.bytes, 108, UINT32_MAX);
  Check(std::get<abcd::abc::MetadataError>(partial_absence.Build()) ==
        abcd::abc::MetadataError::kIndexRange);

  Fixture ordered;
  ordered.bytes[174] = 5;
  ordered.bytes[179] = 1;
  Put32(ordered.bytes, 180, 360);
  Check(std::get<abcd::abc::MetadataError>(ordered.Build()) ==
        abcd::abc::MetadataError::kTagOrder);
  ordered = Fixture();
  ordered.bytes[140] = 0;
  ordered.bytes[141] = 0;
  ordered.bytes[142] = 7;
  Put32(ordered.bytes, 143, 350);
  ordered.bytes[147] = 2;
  ordered.bytes[148] = 0;
  ordered.bytes[149] = 0;
  Check(std::get<abcd::abc::MetadataError>(ordered.Build()) ==
        abcd::abc::MetadataError::kTagOrder);
  ordered = Fixture();
  ordered.bytes[141] = 0;
  ordered.bytes[152] = 3;
  Put32(ordered.bytes, 153, 350);
  ordered.bytes[157] = 2;
  Put32(ordered.bytes, 158, 1);
  ordered.bytes[162] = 0;
  Check(std::get<abcd::abc::MetadataError>(ordered.Build()) ==
        abcd::abc::MetadataError::kTagOrder);

  Fixture capacity;
  capacity.bytes[330] = 41;
  for (std::size_t i = 0; i < 20; ++i) capacity.bytes[331 + i] = 'm';
  capacity.bytes[351] = 0;
  constexpr std::size_t kBaseBudget = sizeof(abcd::abc::RecordInfo) +
                                      sizeof(abcd::abc::MethodInfo) +
                                      sizeof(std::uint32_t) + 40 + 5;
  Check(std::get<abcd::abc::MetadataError>(capacity.Build(kBaseBudget + 20)) ==
        abcd::abc::MetadataError::kResourceLimit);

  fixture.bytes[129] = 'X';
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kBadString);
  fixture.bytes[129] = 'L';

  Put32(fixture.bytes, 96, UINT32_MAX);
  Put32(fixture.bytes, 100, UINT32_MAX);
  Put32(fixture.bytes, 104, UINT32_MAX);
  Put32(fixture.bytes, 108, UINT32_MAX);
  Put16(fixture.bytes, 167, UINT16_MAX);
  result = fixture.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  Check(!std::get<abcd::abc::MetadataIndex>(result)
             .methods[0]
             .proto_offset.has_value());
  fixture.header.profile = abcd::abc::ProfileId::kDynamic9;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kIndexRange);
  fixture.header.profile = abcd::abc::ProfileId::kDynamic1206;

  fixture.bytes[174] = 0xff;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kUnknownTag);
  fixture.bytes[174] = 1;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build(64)) ==
        abcd::abc::MetadataError::kResourceLimit);
  fixture.bytes[165] = 1;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kIndexRange);
  fixture.bytes[165] = 0;
  fixture.bytes[362] = 0xff;
  fixture.bytes[363] = 0x7f;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kCodeRange);
  fixture.bytes[362] = 3;
  fixture.bytes[363] = 0;
  fixture.bytes[179] = 1;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kDuplicateTag);
  fixture.bytes[179] = 3;
  fixture.bytes[340] = 2;
  fixture.bytes[341] = 0xc3;
  fixture.bytes[342] = 0xa9;
  fixture.bytes[343] = 0;
  Put32(fixture.bytes, 169, 340);
  result = fixture.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  Check(std::get<abcd::abc::MetadataIndex>(result).methods[0].name ==
        std::string("\xc3\xa9"));
  fixture.bytes[342] = 'x';
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kBadString);
  fixture.bytes[342] = 0xa9;
  fixture.bytes[340] = 4;
  fixture.bytes[341] = 0xf0;
  fixture.bytes[342] = 0x9f;
  fixture.bytes[343] = 0x98;
  fixture.bytes[344] = 0x80;
  fixture.bytes[345] = 0;
  result = fixture.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  Check(std::get<abcd::abc::MetadataIndex>(result).methods[0].name ==
        std::string("\xf0\x9f\x98\x80"));
  fixture.bytes[342] = 0x8f;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kBadString);
  fixture.bytes[341] = 0xf4;
  fixture.bytes[342] = 0x90;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kBadString);
  fixture.bytes[341] = 0xed;
  fixture.bytes[342] = 0xa0;
  fixture.bytes[343] = 0xbd;
  fixture.bytes[344] = 0xed;
  fixture.bytes[345] = 0xb8;
  fixture.bytes[346] = 0x80;
  fixture.bytes[347] = 0;
  result = fixture.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  Check(std::get<abcd::abc::MetadataIndex>(result).methods[0].name ==
        std::string("\xed\xa0\xbd\xed\xb8\x80"));
  fixture.header.layout =
      abcd::abc::DynamicLayout{{2, 64}, {0, 0}, {0, 0}, {1, 72}};
  Put32(fixture.bytes, 68, 128);
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kIndexRange);
  fixture.header.layout =
      abcd::abc::DynamicLayout{{1, 64}, {0, 0}, {0, 0}, {1, 72}};
  fixture.bytes[72] = 0;
  Check(std::get<abcd::abc::MetadataError>(fixture.Build()) ==
        abcd::abc::MetadataError::kRegionRange);

  Fixture many;
  many.bytes.resize(16000);
  many.header.file_size = many.bytes.size();
  many.header.foreign_offset = 15000;
  many.header.foreign_size = 1000;
  many.header.layout =
      abcd::abc::DynamicLayout{{201, 1024}, {0, 0}, {0, 0}, {1, 72}};
  Put32(many.bytes, 76, 15000);
  Put32(many.bytes, 84, 2000);
  Put32(many.bytes, 2000, 128);
  Put32(many.bytes, 1024, 128);
  for (std::uint32_t i = 1; i <= 200; ++i) {
    const std::uint32_t offset = 4096 + i * 16;
    Put32(many.bytes, 1024 + i * 4, offset);
    many.bytes[offset] = 7;
    many.bytes[offset + 1] = 'L';
    many.bytes[offset + 2] = 'A';
    many.bytes[offset + 3] = ';';
    many.bytes[offset + 4] = 0;
    // Four zero superclass bytes, then flags/counts/end tag.
  }
  result = many.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  Check(std::get<abcd::abc::MetadataIndex>(result).records.size() == 201);
  many.header.layout =
      abcd::abc::DynamicLayout{{202, 1024}, {0, 0}, {0, 0}, {1, 72}};
  Put32(many.bytes, 1024 + 201 * 4, 15010);
  result = many.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  Check(std::get<abcd::abc::MetadataIndex>(result).records.size() == 201);

  Fixture regions;
  regions.bytes.resize(32768);
  regions.header.file_size = regions.bytes.size();
  regions.header.foreign_offset = 32000;
  regions.header.foreign_size = 768;
  constexpr std::uint32_t kRegionTable = 16384;
  regions.header.layout =
      abcd::abc::DynamicLayout{{1, 64}, {0, 0}, {0, 0}, {200, kRegionTable}};
  for (std::uint32_t i = 0; i < 201; ++i) {
    const auto base = kRegionTable + i * 40;
    Put32(regions.bytes, base, i == 0 ? 120 : 1000 + i * 50);
    Put32(regions.bytes, base + 4, i == 0 ? 480 : 1050 + i * 50);
    if (i == 0) {
      Put32(regions.bytes, base + 8, 1);
      Put32(regions.bytes, base + 12, 112);
      Put32(regions.bytes, base + 32, 1);
      Put32(regions.bytes, base + 36, 116);
    }
  }
  result = regions.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
  regions.header.layout =
      abcd::abc::DynamicLayout{{1, 64}, {0, 0}, {0, 0}, {201, kRegionTable}};
  result = regions.Build();
  Check(std::holds_alternative<abcd::abc::MetadataIndex>(result));
}
