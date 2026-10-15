#include <algorithm>
#include <iostream>

#include "abcd/abc/concurrent_requests.h"
#include "abcd/abc/record_attributes.h"
#include "abcd/corpus/abc_file.h"
using namespace abcd;
template <class T, class... U>
T Take(std::variant<U...> value) {
  if (auto* p = std::get_if<T>(&value)) return std::move(*p);
  std::abort();
}
void Check(bool b) {
  if (!b) std::abort();
}
int main(int argc, char** argv) {
  Check(argc == 2);
  auto file =
      Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash = Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index =
      Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  struct Expected {
    unsigned record, method;
    std::optional<unsigned> request;
  };
  for (auto e : {Expected{10719, 11357, 1}, Expected{16446, 16922, {}},
                 Expected{18164, 18358, 1}, Expected{18164, 18419, {}},
                 Expected{31477, 31665, {}}, Expected{25688, 25816, 0}}) {
    auto r =
        std::find_if(index.records.begin(), index.records.end(),
                     [&](const auto& r) { return r.offset.value == e.record; });
    auto m =
        std::find_if(index.methods.begin(), index.methods.end(),
                     [&](const auto& m) { return m.offset.value == e.method; });
    Check(r != index.records.end() && m != index.methods.end());
    const auto attrs = Take<abc::RecordAttributes>(
        abc::ReadMergedRecordAttributes(*file, header, index, *r));
    Check(attrs.module.has_value());
    const auto module = Take<abc::ModuleInfo>(abc::ReadModuleInfo(
        *file, header, *attrs.module, {}, attrs.request_phases));
    const auto read = Take<abc::ConcurrentRequests>(abc::ReadConcurrentRequests(
        *file, header, index, *m, r->offset, module.requests));
    Check(read.present &&
          read.entries.size() == unsigned(e.request.has_value()));
    if (e.request)
      Check(read.entries[0].request.value == *e.request &&
            read.entries[0].specifier == module.requests[*e.request]);
    std::cout << e.record << " m" << e.method
              << " present annotation=" << read.first_annotation->value
              << " requests=[";
    if (e.request) std::cout << *e.request;
    std::cout << "] bytes=" << read.allocated_bytes << " work=" << read.work
              << '\n';
  }
}
