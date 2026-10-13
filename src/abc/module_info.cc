#include "abcd/abc/module_info.h"

#include <new>

#include "abcd/corpus/abc_file.h"
#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
// ets_runtime f04900c: ecmascript/module/accessor/module_data_accessor.cpp.
constexpr std::size_t kEntityBytes = sizeof(std::uint32_t);
constexpr std::size_t kRequestBytes = sizeof(std::uint16_t);

class ModuleReader {
 public:
  ModuleReader(const corpus::AbcFile& file, const Header& header,
               EntityOffset offset, ModuleBudget budget)
      : cursor_(file, offset.value, header.file_size),
        limit_(header.file_size),
        budget_(budget.bytes) {}

  ModuleResult Read(std::optional<EntityOffset> phases) {
    ModuleInfo module;
    if (!Take(cursor_.U32(), &module.encoded_count)) return failure_;
    if (!Section(&module.requests, kEntityBytes,
                 [&](auto* value) { return Entity(value); }) ||
        !Section(&module.imports, 2 * kEntityBytes + kRequestBytes,
                 [&](auto* value) {
                   return Entity(&value->local) && Entity(&value->imported) &&
                          Request(&value->request, module.requests.size());
                 }) ||
        !Section(&module.namespace_imports, kEntityBytes + kRequestBytes,
                 [&](auto* value) {
                   return Entity(&value->local) &&
                          Request(&value->request, module.requests.size());
                 }) ||
        !Section(&module.local_exports, 2 * kEntityBytes,
                 [&](auto* value) {
                   return Entity(&value->local) && Entity(&value->exported);
                 }) ||
        !Section(&module.indirect_exports, 2 * kEntityBytes + kRequestBytes,
                 [&](auto* value) {
                   return Entity(&value->exported) &&
                          Entity(&value->imported) &&
                          Request(&value->request, module.requests.size());
                 }) ||
        !Section(&module.star_exports, kRequestBytes, [&](auto* value) {
          return Request(value, module.requests.size());
        }))
      return failure_;
    module.end.value = cursor_.Position();
    if (phases) {
      // f04900cf module_data_extractor.cpp:177: u32 count, then one byte
      // per request (zero eager, any nonzero lazy), not tagged literal entries.
      if (phases->value < kDynamicHeaderSize || phases->value >= limit_)
        return ModuleFailure{ModuleError::kRequestPhases, *phases};
      if (!cursor_.Seek(phases->value)) return ModuleFailure{ModuleError::kRequestPhases, *phases};
      std::uint32_t count = 0;
      if (!Take(cursor_.U32(), &count)) return failure_;
      if (count != module.requests.size())
        return ModuleFailure{ModuleError::kRequestPhases, *phases};
      if (count > limit_ - cursor_.Position())
        return ModuleFailure{ModuleError::kTruncated, *phases};
      if (count > (budget_ - spent_) / sizeof(ModuleRequestPhase))
        return ModuleFailure{ModuleError::kResourceLimit, *phases};
      module.request_phases.reserve(count);
      if (module.request_phases.capacity() > (budget_ - spent_) / sizeof(ModuleRequestPhase))
        return ModuleFailure{ModuleError::kResourceLimit, *phases};
      spent_ += module.request_phases.capacity() * sizeof(ModuleRequestPhase);
      for (std::uint32_t i = 0; i < count; ++i) {
        std::uint8_t flag = 0;
        if (!Take(cursor_.U8(), &flag)) return failure_;
        module.request_phases.push_back(flag ? ModuleRequestPhase::kLazy : ModuleRequestPhase::kEager);
      }
    }
    return module;
  }

 private:
  bool Fail(ModuleError kind) {
    failure_ = {kind, {cursor_.Position()}};
    return false;
  }
  template <typename T>
  bool Take(const std::variant<T, MetadataError>& result, T* value) {
    if (const auto* error = std::get_if<MetadataError>(&result))
      return Fail(*error == MetadataError::kRead ? ModuleError::kRead
                                                 : ModuleError::kTruncated);
    *value = std::get<T>(result);
    return true;
  }
  bool Entity(EntityOffset* offset) {
    if (!Take(cursor_.U32(), &offset->value)) return false;
    return (offset->value >= kDynamicHeaderSize && offset->value < limit_) ||
           Fail(ModuleError::kReference);
  }
  bool Request(ModuleRequestId* request, std::size_t count) {
    if (!Take(cursor_.U16(), &request->value)) return false;
    return request->value < count || Fail(ModuleError::kRequestIndex);
  }
  template <typename T, typename ReadEntry>
  bool Section(std::vector<T>* entries, std::size_t encoded_bytes,
               ReadEntry read) {
    std::uint32_t count = 0;
    if (!Take(cursor_.U32(), &count)) return false;
    if (count > (limit_ - cursor_.Position()) / encoded_bytes)
      return Fail(ModuleError::kTruncated);
    if (count > (budget_ - spent_) / sizeof(T))
      return Fail(ModuleError::kResourceLimit);
    entries->reserve(count);
    if (entries->capacity() > (budget_ - spent_) / sizeof(T))
      return Fail(ModuleError::kResourceLimit);
    spent_ += entries->capacity() * sizeof(T);
    for (std::uint32_t i = 0; i < count; ++i) {
      T entry{};
      if (!read(&entry)) return false;
      entries->push_back(entry);
    }
    return true;
  }
  MetadataCursor cursor_;
  std::uint32_t limit_;
  std::size_t budget_;
  std::size_t spent_ = 0;
  ModuleFailure failure_{ModuleError::kTruncated, {}};
};

bool Supported(ProfileId profile) {
  switch (profile) {
    case ProfileId::kDynamic9:
    case ProfileId::kDynamic1102:
    case ProfileId::kDynamic1202:
    case ProfileId::kDynamic1206:
    case ProfileId::kDynamic1301:
    case ProfileId::kDynamic2400:
      return true;
    case ProfileId::kUnknown:
    case ProfileId::kStatic0107:
      return false;
  }
  return false;
}
}  // namespace

ModuleResult ReadModuleInfo(const corpus::AbcFile& file, const Header& header,
                            EntityOffset offset, ModuleBudget budget, std::optional<EntityOffset> phases) {
  if (header.kind != AbcKind::kDynamic || !Supported(header.profile) ||
      !std::holds_alternative<DynamicLayout>(header.layout))
    return ModuleFailure{ModuleError::kUnsupportedProfile, offset};
  if (header.file_size != file.Size() || file.CheckUnchanged())
    return ModuleFailure{ModuleError::kFileChanged, offset};
  if (offset.value < kDynamicHeaderSize || offset.value >= header.file_size)
    return ModuleFailure{ModuleError::kReference, offset};
  try {
    ModuleReader reader(file, header, offset, budget);
    auto result = reader.Read(phases);
    if (file.CheckUnchanged())
      return ModuleFailure{ModuleError::kFileChanged, offset};
    return result;
  } catch (const std::bad_alloc&) {
    return ModuleFailure{ModuleError::kResourceLimit, offset};
  }
}

}  // namespace abcd::abc
