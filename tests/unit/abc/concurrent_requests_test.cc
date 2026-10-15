#include "abcd/abc/concurrent_requests.h"

#include <unistd.h>

#include <cstdlib>

#include "abcd/corpus/abc_file.h"
using namespace abcd::abc;
namespace {
void Check(bool ok) {
  if (!ok) std::abort();
}
struct Fixture {
  std::vector<std::uint8_t> bytes;
  Header header;
  MetadataIndex index;
  MethodInfo method;
  RecordInfo record;
  std::vector<EntityOffset> requests;
  explicit Fixture(unsigned count = 1)
      : bytes(1024 + count * 16), requests(count) {
    header.kind = AbcKind::kDynamic;
    header.profile = ProfileId::kDynamic2400;
    header.layout = DynamicLayout{};
    header.file_size = bytes.size();
    record.offset = {80};
    method.offset = {96};
    method.owner_offset = record.offset;
    method.annotations = {{128}, 1};
    bytes[128] = 6;
    Word(129, 512);
    index.regions.push_back({500, header.file_size, {1, 192}, {}, {}, {}});
    Word(192, 256);
    String(256, "L_ESConcurrentModuleRequestsAnnotation;");
    String(320, "ConcurrentModuleRequest");
    bytes[514] = count;
    bytes[515] = count >> 8;
    for (unsigned i = 0; i < count; ++i) {
      Word(516 + 8 * i, 320);
      Word(520 + 8 * i, i);
      bytes[516 + 8 * count + i] = '7';
      requests[i] = {400};
    }
  }
  void Word(unsigned at, unsigned v) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = v >> (i * 8);
  }
  void String(unsigned at, std::string_view v) {
    bytes[at++] = (v.size() << 1) | 1;
    for (char c : v) bytes[at++] = c;
    bytes[at] = 0;
  }
  auto Read(ConcurrentRequestBudget budget = {}) {
    char path[] = "/tmp/abcd-concurrent-XXXXXX";
    int fd = mkstemp(path);
    Check(fd >= 0);
    Check(write(fd, bytes.data(), bytes.size()) == ssize_t(bytes.size()));
    close(fd);
    auto opened = abcd::corpus::AbcFile::Open(path);
    Check(
        std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
    auto result = ReadConcurrentRequests(
        *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened), header,
        index, method, record.offset, requests, budget);
    unlink(path);
    return result;
  }
};
}  // namespace
int main() {
  Fixture f;
  auto r = std::get<ConcurrentRequests>(f.Read());
  Check(r.present && r.entries.size() == 1 && r.entries[0].request.value == 0 &&
        r.entries[0].specifier.value == 400);
  Fixture empty(0);
  Check(std::get<ConcurrentRequests>(empty.Read()).present);
  empty.method.annotations.count = 0;
  Check(!std::get<ConcurrentRequests>(empty.Read()).present);
  f.Word(520, 1);
  Check(std::get<ConcurrentRequestFailure>(f.Read()).kind ==
        ConcurrentRequestError::kRequest);
  f.Word(520, 0);
  f.bytes[524] = 'I';
  Check(std::get<ConcurrentRequestFailure>(f.Read()).kind ==
        ConcurrentRequestError::kElementType);
  f.bytes[524] = '7';
  f.record.offset = {81};
  Check(std::get<ConcurrentRequestFailure>(f.Read()).kind ==
        ConcurrentRequestError::kOwner);
  f.record.offset = {80};
  f.Word(516, 400);
  Check(std::get<ConcurrentRequestFailure>(f.Read()).kind ==
        ConcurrentRequestError::kElementName);
  Fixture malformed;
  malformed.Word(129, malformed.header.file_size - 1);
  Check(std::get<ConcurrentRequestFailure>(malformed.Read()).kind ==
        ConcurrentRequestError::kRead);
  Fixture unrelated;
  unrelated.String(256, "L_Unrelated;");
  Check(!std::get<ConcurrentRequests>(unrelated.Read()).present);
  Fixture mixed(2);
  mixed.Word(528, 0);
  mixed.Word(520, 1);
  auto ordered = std::get<ConcurrentRequests>(mixed.Read());
  Check(ordered.entries[0].request.value == 1 &&
        ordered.entries[1].request.value == 0);
  // Preserve encoded order and duplicates; runtime's set is not wire evidence.
  Fixture duplicate(2);
  duplicate.Word(528, 0);
  auto d = std::get<ConcurrentRequests>(duplicate.Read());
  Check(d.entries.size() == 2 && d.entries[1].request.value == 0);
  for (unsigned n : {200u, 201u, 400u, 800u}) {
    Fixture scale(n);
    auto proof = std::get<ConcurrentRequests>(scale.Read());
    Check(proof.entries.size() == n);
    Check(proof.allocated_bytes == n * sizeof(ConcurrentRequest));
    Check(std::holds_alternative<ConcurrentRequests>(
        scale.Read({proof.allocated_bytes, proof.work})));
    Check(std::get<ConcurrentRequestFailure>(
              scale.Read({proof.allocated_bytes - 1, proof.work}))
              .kind == ConcurrentRequestError::kResourceLimit);
    Check(std::get<ConcurrentRequestFailure>(
              scale.Read({proof.allocated_bytes, proof.work - 1}))
              .kind == ConcurrentRequestError::kWorkLimit);
    Check(proof.work < 1000ull * (n + 1));
  }
}
