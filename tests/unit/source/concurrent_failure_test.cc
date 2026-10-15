#include <unistd.h>

#include <cstdlib>
#include <iostream>

#include "../../../src/source/concurrent_context.h"
#include "../../../src/source/concurrent_failure_json.h"
#include "../../../src/source/family_route_failure.h"
#include "../../../tools/record_failure_json.h"
#include "abcd/corpus/abc_file.h"
using namespace abcd;
using namespace abcd::source;
void Check(bool b) {
  if (!b) std::abort();
}
void RoundTrip(ConcurrentFailure e,
               CreationContextError kind = CreationContextError::kKind) {
  auto route =
      FamilyRouteFailure(FamilyPrepareFailure{CreationContextFailure{kind, e}});
  Check(route.concurrent == e && route.method == e.method && !route.pc &&
        !route.working_pc);
  Check(route.creation_method == e.creator &&
        route.creation_pc == e.creation_pc);
  Check(route.causes.count >= 2 &&
        route.causes.entries[1].domain == FailureDomain<ConcurrentError>());
  Check(route.failure_class == (kind == CreationContextError::kResourceLimit
                                    ? RouteFailureClass::kMemoryLimit
                                : kind == CreationContextError::kWorkLimit
                                    ? RouteFailureClass::kWorkLimit
                                    : RouteFailureClass::kUnproved));
  MethodRouteReceipt receipt;
  receipt.identity.file_size = 4096;
  receipt.identity.file_hash.bytes[0] = 1;
  receipt.identity.abc_version = {12, 0, 6, 0};
  receipt.identity.profile = abc::ProfileId::kDynamic1206;
  receipt.method = {77};
  receipt.record = {111};
  receipt.stage = SourceStage::kValuesVerified;
  receipt.work_revision = 0;
  receipt.coverage = Coverage::kUnsupported;
  receipt.failure = route;
  auto text = SerializeRouteReceipt(receipt);
  Check(std::holds_alternative<std::string>(text));
  auto parsed = ParseRouteReceipt(std::get<std::string>(text));
  Check(std::holds_alternative<MethodRouteReceipt>(parsed));
  Check(std::get<MethodRouteReceipt>(parsed).failure->concurrent == e);
  nlohmann::json a, b;
  tools::DescribeRecordFailure(RouteRefusal{route}, a);
  RecordProgramFailure entry{RecordProgramError::kFunction, {}};
  entry.concurrent = e;
  entry.causes = route.causes;
  tools::DescribeRecordFailure(entry, b);
  Check(b["source_concurrent"] == a["source_failure"]["concurrent"]);
  Check(b["source_causes"] == a["source_failure"]["causes"]);
}
// No module field: the work charge must precede even the early kModule exit.
void AttributeWork() {
  std::vector<std::uint8_t> bytes(8192, 0);
  unsigned at = 128, length = 4096, encoded = (length << 1) | 1;
  do {
    bytes[at++] = (encoded & 127) | (encoded >= 128 ? 128 : 0);
    encoded >>= 7;
  } while (encoded);
  std::fill_n(bytes.begin() + at, length, 'x');
  char path[] = "/tmp/abcd-concurrent-work-XXXXXX";
  int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size()));
  close(fd);
  auto opened = corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic2400;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  abc::RecordInfo record;
  record.offset = {80};
  index.records.push_back(record);
  abc::MethodInfo method;
  method.offset = {96};
  method.owner_offset = record.offset;
  for (unsigned count : {1u, 200u, 201u, 400u, 800u}) {
    index.records[0].field_count = count;
    index.fields.assign(count, abc::FieldInfo{});
    for (auto& field : index.fields) {
      field.offset = {100};
      field.owner_offset = record.offset;
      field.name_offset = {128};  // Repeated alias must not evade the charge.
    }
    auto failure = InspectConcurrentCreation(
        file, header, index, method, {}, {}, SourceContract::kLanguageSourceV3,
        {1u << 20, count + 10u});
    Check(failure.kind == ConcurrentError::kWorkLimit);
  }
  unlink(path);
}
int main() {
  AttributeWork();
  static_assert(std::is_trivially_copyable_v<ConcurrentFailure>);
  for (unsigned kind = 0;
       kind <= static_cast<unsigned>(ConcurrentError::kWorkLimit); ++kind) {
    ConcurrentFailure e;
    e.kind = static_cast<ConcurrentError>(kind);
    e.method = {77};
    e.record = {111};
    e.creator = {88};
    RoundTrip(e);
    e.annotation_present = false;
    e.request_count = 0;
    RoundTrip(e);
    e.annotation_present = true;
    e.annotation = abc::EntityOffset{200};
    RoundTrip(e);
    e.request_count = 1;
    e.cause = abc::ConcurrentRequestError::kElementType;
    e.element = 0;
    e.request = 0;
    RoundTrip(e);
    e.metadata = abc::MetadataError::kBadString;
    RoundTrip(e);
    RoundTrip(e, CreationContextError::kResourceLimit);
    RoundTrip(e, CreationContextError::kWorkLimit);
    auto json = nlohmann::json::parse(ConcurrentJson(e).dump());
    json["kind"] = UINT32_MAX;
    Check(!ParseConcurrent(json));
    json = nlohmann::json::parse(ConcurrentJson(e).dump());
    json["creation_pc"] = -1;
    Check(!ParseConcurrent(json));
    json = nlohmann::json::parse(ConcurrentJson(e).dump());
    json["annotation_present"] = false;
    Check(!ParseConcurrent(json));
  }
  std::cout << "concurrent failure: fixed-size witnesses, PC0 versus absent "
               "child PC, resource classes and both JSON paths\n";
}
