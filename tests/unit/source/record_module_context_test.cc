#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

#include "abcd/corpus/abc_file.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_source.h"
#include "abcd/analysis/method_roles.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
void Word(std::vector<std::uint8_t>& bytes, std::size_t offset,
          std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void String(std::vector<std::uint8_t>& bytes, std::size_t offset,
            std::string_view value) {
  Check(value.size() < 64);
  bytes[offset++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
  std::copy(value.begin(), value.end(), bytes.begin() + offset);
}
}  // namespace

int main() {
  using namespace abcd;
  constexpr std::uint32_t kTypeTable = 200;
  constexpr std::uint32_t kRecord = 500;
  constexpr std::uint32_t kModule = 420;
  constexpr std::uint32_t kRequest = 560;
  std::vector<std::uint8_t> bytes(640);
  String(bytes, abc::kDynamicHeaderSize, "moduleRecordIdx");
  String(bytes, 90, "isCommonjs");
  String(bytes, 112, "isSharedModule");
  String(bytes, 144, "hasTopLevelAwait");
  String(bytes, kRequest, "./dependency.mjs");
  bytes[kTypeTable] = 5;
  Word(bytes, kModule + 4, 1);
  Word(bytes, kModule + 8, kRequest);
  char path[] = "/tmp/abcd-record-module-XXXXXX";
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
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({250, 600, {1, kTypeTable}, {}, {}, {}});
  abc::RecordInfo record;
  record.offset = {kRecord};
  record.field_count = 1;
  index.fields.push_back({{300}, record.offset,
                          {abc::kDynamicHeaderSize}, {}, 0,
                          abc::FieldValue{abc::FieldValueEncoding::kSignedLeb,
                                          kModule}});
  auto read = [&] {
    return source::ReadRecordModuleContext(
        file, header, index, record, abc::FilePackaging::kMerged);
  };
  const auto result = read();
  const auto& context = std::get<source::RecordModuleContext>(result);
  Check(context.record() == record.offset);
  Check(context.OrdinarySynchronous(file, record.offset));
  Check(!context.OrdinarySynchronous(file, {kRecord + 1}));
  Check(context.ForOwner(file, record.offset) != nullptr);
  Check(context.ForOwner(file, {kRecord + 1}) == nullptr);
  Check(context.ForOwner(file, record.offset)->requests[0].value == kRequest);
  const auto lookup_result = abc::MethodLookup::Build(index.methods);
  const auto& lookup = std::get<abc::MethodLookup>(lookup_result);
  const auto roles_result = analysis::MethodRoleIndex::Build(
      file, header, index, lookup);
  const auto& roles = std::get<analysis::MethodRoleIndex>(roles_result);
  // The empty method inventory has no valid record entry: the value-tree
  // record route reports a typed entry failure without fabricating metadata.
  const source::RouteInputIdentity identity{};
  const auto missing_entry = source::BuildRecordSource(file, header, index,
      lookup, record, context, roles, {}, identity);
  const auto* entry_failure =
      std::get_if<source::RecordProgramFailure>(&missing_entry);
  Check(entry_failure &&
        entry_failure->kind == source::RecordProgramError::kEntry);
  auto second_opened = corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(second_opened));
  const auto& second = *std::get<std::unique_ptr<corpus::AbcFile>>(second_opened);
  Check(context.ForOwner(second, record.offset) == nullptr);
  Check(!context.OrdinarySynchronous(second, record.offset));
  std::shared_ptr<const abc::ModuleInfo> retained;
  {
    const auto temporary = read();
    retained = std::get<source::RecordModuleContext>(temporary)
                   .ForOwner(file, record.offset);
  }
  Check(retained && retained->requests[0].value == kRequest);
  for (const std::uint32_t name : {112U,144U}) {
    index.fields.push_back({{310}, record.offset, {name}, {}, 0,
        abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, 1}});
    record.field_count = 2;
    const auto restricted = read();
    Check(!std::get<source::RecordModuleContext>(restricted).OrdinarySynchronous(file,record.offset));
    index.fields.pop_back(); record.field_count = 1;
  }
  index.fields.push_back({{310}, record.offset, {90}, {}, 0,
                          abc::FieldValue{abc::FieldValueEncoding::kSignedLeb,
                                          1}});
  record.field_count = 2;
  const auto common_js = read();
  Check(std::get<source::RecordModuleError>(
            std::get<source::RecordModuleFailure>(common_js).reason) ==
        source::RecordModuleError::kCommonJs);
  index.fields[0].value->bits = UINT32_MAX;
  const auto common_js_only = read();
  Check(std::get<source::RecordModuleError>(
            std::get<source::RecordModuleFailure>(common_js_only).reason) ==
        source::RecordModuleError::kCommonJs);
  index.fields[0].value->bits = kModule;
  record.field_count = 1;
  index.fields.pop_back();
  index.fields[0].value->bits = UINT32_MAX;
  const auto absent = read();
  Check(std::get<source::RecordModuleContext>(absent)
            .ForOwner(file, record.offset) == nullptr);
  index.fields[0].value->bits = 639;
  const auto malformed = read();
  Check(std::holds_alternative<abc::ModuleFailure>(
      std::get<source::RecordModuleFailure>(malformed).reason));
  index.fields[0].value->bits = kModule;
  index.fields[0].owner_offset = {kRecord + 1};
  const auto foreign = read();
  Check(std::holds_alternative<abc::AttributeFailure>(
      std::get<source::RecordModuleFailure>(foreign).reason));
  const auto bundle = source::ReadRecordModuleContext(
      file, header, index, record, abc::FilePackaging::kBundle);
  Check(std::get<source::RecordModuleError>(
            std::get<source::RecordModuleFailure>(bundle).reason) ==
        source::RecordModuleError::kBundleOwnership);
  Check(unlink(path) == 0);
}
