#include "abcd/abc/record_attributes.h"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd::abc;
  std::vector<std::uint8_t> bytes(640);
  MetadataIndex index;
  RecordInfo record;
  record.offset = {500};
  const std::array<std::string_view, 6> names{
      "isCommonjs",     "jsonFileContent",  "moduleRecordIdx",
      "isSharedModule", "hasTopLevelAwait", "moduleRequestPhaseIdx"};
  const std::array<std::uint32_t, 6> values{0, 400, 420, 2, 1, 440};
  std::size_t pos = kDynamicHeaderSize;
  for (std::size_t i = 0; i < names.size(); ++i) {
    FieldInfo field;
    field.offset = {static_cast<std::uint32_t>(300 + i * 10)};
    field.owner_offset = record.offset;
    field.name_offset = {static_cast<std::uint32_t>(pos)};
    field.value = FieldValue{FieldValueEncoding::kSignedLeb, values[i]};
    bytes[pos++] = static_cast<std::uint8_t>((names[i].size() << 1) | 1);
    for (const char byte : names[i]) bytes[pos++] = byte;
    bytes[pos++] = 0;
    index.fields.push_back(field);
  }
  Check(pos < 200);
  bytes[200] = 5;   // i32 field encoding (type code 7 minus u1 code 2).
  bytes[204] = 8;   // f64
  bytes[208] = 9;   // i64
  bytes[212] = 10;  // u64
  index.regions.push_back({250, 600, {4, 200}, {}, {}, {}});
  record.field_count = index.fields.size();
  char path[] = "/tmp/abcd-record-attributes-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  close(fd);
  auto opened = abcd::corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened);
  Header header;
  header.kind = AbcKind::kDynamic;
  header.profile = ProfileId::kDynamic1206;
  header.layout = DynamicLayout{};
  header.file_size = bytes.size();
  Check(std::get<FilePackaging>(DetectFilePackaging(file, header, index)) ==
        FilePackaging::kMerged);
  MetadataIndex packaging;
  Check(std::get<FilePackaging>(DetectFilePackaging(file, header, packaging)) ==
        FilePackaging::kBundle);
  packaging.fields.push_back(index.fields[1]);  // JSON alone is not a witness.
  Check(std::get<FilePackaging>(DetectFilePackaging(file, header, packaging)) ==
        FilePackaging::kBundle);
  packaging.fields.push_back(index.fields[2]);
  packaging.fields.back().value.reset();  // Packaging depends on name only.
  Check(std::get<FilePackaging>(DetectFilePackaging(file, header, packaging)) ==
        FilePackaging::kMerged);
  Check(
      std::get<AttributeFailure>(DetectFilePackaging(file, header, index, {0}))
          .kind == AttributeError::kName);
  auto unsupported = header;
  unsupported.profile = ProfileId::kStatic0107;
  Check(std::get<AttributeFailure>(
            DetectFilePackaging(file, unsupported, packaging))
            .kind == AttributeError::kUnsupportedProfile);
  auto result = ReadMergedRecordAttributes(file, header, index, record);
  const auto attributes = std::get<RecordAttributes>(result);
  Check(attributes.common_js.has_value() && !*attributes.common_js);
  Check(attributes.json_content->value == 400);
  Check(attributes.module->value == 420);
  Check(*attributes.shared && *attributes.top_level_await);
  Check(attributes.request_phases->value == 440);
  index.fields[2].value->bits = UINT32_MAX;
  index.fields[5].value->bits = 0;
  const auto absent = std::get<RecordAttributes>(
      ReadMergedRecordAttributes(file, header, index, record));
  Check(!absent.module && !absent.request_phases);
  index.fields[2].value->bits = 0;
  Check(std::get<AttributeFailure>(
            ReadMergedRecordAttributes(file, header, index, record))
            .kind == AttributeError::kReference);
  index.fields[2].value->bits = 420;
  index.fields[2].value->encoding = FieldValueEncoding::kWord;
  Check(std::get<RecordAttributes>(
            ReadMergedRecordAttributes(file, header, index, record))
            .module->value == 420);
  for (std::uint16_t type = 1; type <= 3; ++type) {
    index.fields[2].type_index.value = type;
    Check(std::get<AttributeFailure>(
              ReadMergedRecordAttributes(file, header, index, record))
              .kind == AttributeError::kWideValue);
  }
  index.fields[2].type_index.value = 4;
  Check(std::get<AttributeFailure>(
            ReadMergedRecordAttributes(file, header, index, record))
            .kind == AttributeError::kTypeReference);
  index.fields[2].type_index.value = 0;
  index.fields[2].value.reset();
  Check(std::get<AttributeFailure>(
            ReadMergedRecordAttributes(file, header, index, record))
            .kind == AttributeError::kMissingValue);
  index.fields[2].value = FieldValue{FieldValueEncoding::kSignedLeb, 420};
  auto duplicate = index.fields.front();
  duplicate.value->bits = 1;
  index.fields.push_back(duplicate);
  record.field_count = 7;
  Check(*std::get<RecordAttributes>(
             ReadMergedRecordAttributes(file, header, index, record))
             .common_js);
  Check(std::get<AttributeFailure>(
            ReadMergedRecordAttributes(file, header, index, record, {0}))
            .kind == AttributeError::kName);
  record.field_count = 8;
  Check(std::get<AttributeFailure>(
            ReadMergedRecordAttributes(file, header, index, record))
            .kind == AttributeError::kFieldRange);
  MetadataIndex bundle;
  auto module_record = record;
  module_record.descriptor = "L_ESModuleRecord;";
  module_record.field_count = 2;
  bundle.records.push_back(module_record);
  bundle.fields.assign(index.fields.begin(), index.fields.begin() + 2);
  bundle.fields[0].value->bits = 420;
  bundle.fields[1].value->bits = 440;
  auto read_bundle = [&](std::string_view descriptor) {
    return ReadBundleRecordAttributes(file, header, bundle, descriptor);
  };
  Check(std::get<RecordAttributes>(read_bundle("entry.abc")).module->value ==
        440);
  Check(std::get<RecordAttributes>(read_bundle("jsonFileContent"))
            .module->value == 420);
  bundle.fields[1].value->bits = UINT32_MAX;
  Check(!std::get<RecordAttributes>(read_bundle("entry.abc")).module);
  bundle.fields[1].value.reset();
  Check(std::get<RecordAttributes>(read_bundle("jsonFileContent"))
            .module->value == 420);
  Check(std::get<AttributeFailure>(read_bundle("entry.abc")).kind ==
        AttributeError::kMissingValue);
  bundle.fields[1].value = FieldValue{FieldValueEncoding::kSignedLeb, 440};
  bundle.records.push_back(module_record);
  bundle.records.back().first_field = 2;
  bundle.records.back().field_count = 1;
  bundle.fields.push_back(bundle.fields[0]);
  bundle.fields.back().value->bits = 460;
  Check(std::get<RecordAttributes>(read_bundle("entry.abc")).module->value ==
        440);
  bundle.fields[1].value->bits = UINT32_MAX;
  Check(std::get<RecordAttributes>(read_bundle("entry.abc")).module->value ==
        460);
  bundle.records.pop_back();
  bundle.fields[0].owner_offset = {};
  Check(std::get<AttributeFailure>(read_bundle("entry.abc")).kind ==
        AttributeError::kOwnerMismatch);
  bundle.fields[0].owner_offset = record.offset;
  bundle.fields[0].name_offset = {header.file_size};
  Check(std::get<AttributeFailure>(read_bundle("entry.abc")).kind ==
        AttributeError::kName);
  bundle.fields[0].name_offset = index.fields[0].name_offset;
  Check(std::get<AttributeFailure>(
            ReadBundleRecordAttributes(file, unsupported, bundle, "entry.abc"))
            .kind == AttributeError::kUnsupportedProfile);
  bundle.records[0].descriptor = "L_CommonJsRecord;";
  Check(*std::get<RecordAttributes>(read_bundle("entry.abc")).common_js);
  bundle.records[0].descriptor = "isSharedModule";
  Check(*std::get<RecordAttributes>(read_bundle("entry.abc")).shared);
  bundle.records[0].descriptor = "L_HasTopLevelAwait;";
  Check(*std::get<RecordAttributes>(read_bundle("entry.abc")).top_level_await);
  unlink(path);
}
