#include "abcd/abc/concurrent_requests.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/abc/profile_capabilities.h"
#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"
namespace abcd::abc {
namespace {
// OH7 AnnotationDataAccessor layout; emitter.cpp
// GenConcurrentModuleRequestsAnnotation.
constexpr std::uint8_t kAnnotationTag = 6, kU32Tag = '7';
constexpr std::uint32_t kWord = sizeof(std::uint32_t), kElement = 2 * kWord,
                        kTaggedReference = 1 + kWord;
constexpr std::string_view kDescriptor =
                               "L_ESConcurrentModuleRequestsAnnotation;",
                           kName = "ConcurrentModuleRequest";
class Reader {
 public:
  Reader(const corpus::AbcFile& f, const Header& h, const MetadataIndex& i,
         const MethodInfo& m, EntityOffset r,
         std::span<const EntityOffset> requests, ConcurrentRequestBudget b)
      : file(f),
        header(h),
        index(i),
        method(m),
        record(r),
        requests(requests),
        budget(b),
        left(b.work) {}
  ConcurrentRequests Run() {
    Require(method.owner_offset == record, ConcurrentRequestError::kOwner);
    if (header.kind != AbcKind::kDynamic ||
        !HasPinnedRuntimeSemantics(header.profile))
      Bad(MetadataError::kUnsupportedProfile);
    if (file.Size() != header.file_size || file.CheckUnchanged())
      Bad(MetadataError::kFileChanged);
    ConcurrentRequests out;
    const auto count = Pass(nullptr, out);
    Require(count <= budget.bytes / sizeof(ConcurrentRequest),
            ConcurrentRequestError::kResourceLimit);
    out.entries.reserve(count);
    Require(out.entries.capacity() <= budget.bytes / sizeof(ConcurrentRequest),
            ConcurrentRequestError::kResourceLimit);
    Pass(&out.entries, out);
    if (file.CheckUnchanged()) Bad(MetadataError::kFileChanged);
    out.allocated_bytes = out.entries.capacity() * sizeof(ConcurrentRequest);
    out.work = budget.work - left;
    return out;
  }
  [[noreturn]] void Fail(ConcurrentRequestError kind) const {
    throw ConcurrentRequestFailure{kind,    method.offset, record, annotation,
                                   element, request,       {}};
  }

 private:
  void Require(bool ok, ConcurrentRequestError kind) const {
    if (!ok) Fail(kind);
  }
  [[noreturn]] void Bad(MetadataError e) const {
    throw ConcurrentRequestFailure{ConcurrentRequestError::kRead,
                                   method.offset,
                                   record,
                                   annotation,
                                   element,
                                   request,
                                   e};
  }
  void Step(std::uint64_t n = 1) {
    Require(n <= left, ConcurrentRequestError::kWorkLimit);
    left -= n;
  }
  template <class T>
  T Take(std::variant<T, MetadataError> v) {
    if (auto* e = std::get_if<MetadataError>(&v)) Bad(*e);
    return std::get<T>(v);
  }
  // All recognized boundary names are ASCII. Compare the complete encoded
  // string, not a prefix, without allocating attacker-sized string storage.
  bool Name(std::uint32_t at, std::string_view expected) {
    Step();
    if (at < kDynamicHeaderSize || at >= header.file_size)
      Bad(MetadataError::kReference);
    MetadataCursor c(file, at, header.file_size);
    const auto length = Take(c.Uleb()) >> 1;
    if (length != expected.size()) return false;
    for (char ch : expected) {
      Step();
      if (Take(c.U8()) != static_cast<std::uint8_t>(ch)) return false;
    }
    Step();
    return Take(c.U8()) == 0;
  }
  std::size_t Pass(std::vector<ConcurrentRequest>* entries,
                   ConcurrentRequests& out) {
    const auto tags = method.annotations;
    std::size_t total = 0;
    if (tags.count == 0) return 0;
    if (tags.first_tag.value < kDynamicHeaderSize ||
        tags.first_tag.value > header.file_size ||
        tags.count >
            (header.file_size - tags.first_tag.value) / kTaggedReference)
      Bad(MetadataError::kReference);
    MetadataCursor stream(file, tags.first_tag.value, header.file_size);
    for (std::uint32_t a = 0; a < tags.count; ++a) {
      Step();
      annotation.reset();
      element.reset();
      request.reset();
      if (Take(stream.U8()) != kAnnotationTag) Bad(MetadataError::kUnknownTag);
      const auto offset = Take(stream.U32());
      annotation = EntityOffset{offset};
      if (offset < kDynamicHeaderSize || offset >= header.file_size)
        Bad(MetadataError::kReference);
      MetadataCursor c(file, offset, header.file_size);
      const auto slot = Take(c.U16()), count = Take(c.U16());
      const auto start = c.Position();
      if (count > (header.file_size - start) / (kElement + 1))
        Bad(MetadataError::kTruncated);
      Step(std::bit_width(index.regions.size()) + 1);
      auto it =
          std::upper_bound(index.regions.begin(), index.regions.end(), offset,
                           [](auto n, const auto& r) { return n < r.start; });
      if (it == index.regions.begin() || offset >= (--it)->end)
        Bad(MetadataError::kRegionRange);
      const auto table = it->classes;
      if (table.offset > header.file_size ||
          table.count > (header.file_size - table.offset) / kWord ||
          slot >= table.count)
        Bad(MetadataError::kIndexRange);
      MetadataCursor cls(file, table.offset + slot * kWord, header.file_size);
      if (!Name(Take(cls.U32()), kDescriptor)) continue;
      out.present = true;
      if (!out.first_annotation) out.first_annotation = annotation;
      Require(count <= std::numeric_limits<std::size_t>::max() - total,
              ConcurrentRequestError::kResourceLimit);
      total += count;
      MetadataCursor types(file, start + count * kElement, header.file_size);
      for (std::uint32_t e = 0; e < count; ++e) {
        Step();
        element = e;
        request.reset();
        const auto name = Take(c.U32()), value = Take(c.U32());
        request = value;
        Require(Name(name, kName), ConcurrentRequestError::kElementName);
        Require(Take(types.U8()) == kU32Tag,
                ConcurrentRequestError::kElementType);
        Require(value < requests.size() && value <= UINT16_MAX,
                ConcurrentRequestError::kRequest);
        if (entries) {
          // A changed file between the count/fill passes must not cause growth
          // beyond the capacity charged before allocation.
          if (entries->size() == entries->capacity())
            Bad(MetadataError::kFileChanged);
          entries->push_back({*annotation,
                              e,
                              {static_cast<std::uint16_t>(value)},
                              requests[value]});
        }
      }
    }
    return total;
  }
  const corpus::AbcFile& file;
  const Header& header;
  const MetadataIndex& index;
  const MethodInfo& method;
  EntityOffset record;
  std::span<const EntityOffset> requests;
  ConcurrentRequestBudget budget;
  std::uint64_t left;
  std::optional<EntityOffset> annotation;
  std::optional<std::uint32_t> element, request;
};
}  // namespace
std::variant<ConcurrentRequests, ConcurrentRequestFailure>
ReadConcurrentRequests(const corpus::AbcFile& f, const Header& h,
                       const MetadataIndex& i, const MethodInfo& m,
                       EntityOffset r, std::span<const EntityOffset> requests,
                       ConcurrentRequestBudget b) {
  try {
    return Reader(f, h, i, m, r, requests, b).Run();
  } catch (const ConcurrentRequestFailure& e) {
    return e;
  } catch (const std::bad_alloc&) {
    return ConcurrentRequestFailure{ConcurrentRequestError::kResourceLimit,
                                    m.offset, r};
  } catch (const std::length_error&) {
    return ConcurrentRequestFailure{ConcurrentRequestError::kResourceLimit,
                                    m.offset, r};
  }
}
}  // namespace abcd::abc
