#include "named_scope.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <utility>

#include "abcd/abc/literal_array.h"
#include "abcd/abc/string_entity.h"
#include "abcd/analysis/lexical.h"

namespace abcd::source {
namespace {
struct ScopeSite {
  std::size_t instruction;
  abc::EntityOffset literal;
  std::uint16_t slots;
};
struct ScopeShape {
  std::optional<std::uint32_t> largest_slot;
  std::size_t names;
};
constexpr std::size_t kScopePairEntries = 2;
// An admission estimate for comparison-based sorting, not a step count.
constexpr std::uint64_t kSortWorkPerLevel = 4;
class ScopeQuota {
 public:
  explicit ScopeQuota(std::uint64_t limit) : remaining_(limit) {}
  bool Take(std::uint64_t units) {
    if (units > remaining_) return false;
    remaining_ -= units;
    used_ += units;
    return true;
  }
  std::uint64_t Used() const { return used_; }

 private:
  std::uint64_t remaining_;
  std::uint64_t used_ = 0;
};

BodyEmitFailure Fail(BodyEmitError kind, abc::CodeOffset offset,
                     std::optional<abc::MetadataError> metadata = {}) {
  return {kind, offset, {}, {}, metadata};
}

std::variant<abc::LiteralArray, BodyEmitFailure> ReadScopeLiteral(
    const MethodConstants& constants, abc::EntityOffset offset,
    abc::CodeOffset instruction, std::size_t bytes, ScopeQuota& quota) {
  auto read = abc::ReadLiteralArray(constants.file, constants.header, offset,
                                    {bytes});
  if (const auto* failure = std::get_if<abc::LiteralFailure>(&read))
    return Fail(failure->kind == abc::LiteralError::kResourceLimit
                    ? BodyEmitError::kResourceLimit
                    : BodyEmitError::kConstant,
                instruction);
  const auto& entries = std::get<abc::LiteralArray>(read).entries;
  if (entries.capacity() > bytes / sizeof(abc::LiteralEntry))
    return Fail(BodyEmitError::kResourceLimit, instruction);
  if (!quota.Take(entries.size()))
    return Fail(BodyEmitError::kWorkLimit, instruction);
  return std::get<abc::LiteralArray>(std::move(read));
}

std::variant<ScopeShape, BodyEmitFailure> InspectLiteral(
    const MethodConstants& constants, abc::EntityOffset offset,
    abc::CodeOffset instruction, std::size_t bytes, ScopeQuota& quota) {
  const auto read =
      ReadScopeLiteral(constants, offset, instruction, bytes, quota);
  if (const auto* failure = std::get_if<BodyEmitFailure>(&read))
    return *failure;
  const auto& entries = std::get<abc::LiteralArray>(read).entries;
  if (entries.empty() || entries.size() % kScopePairEntries != 1 ||
      entries.front().tag != abc::LiteralTag::kInteger)
    return Fail(BodyEmitError::kConstant, instruction);
  ScopeShape shape{{}, (entries.size() - 1) / kScopePairEntries};
  for (std::size_t i = 1; i < entries.size(); i += kScopePairEntries) {
    if (entries[i].tag != abc::LiteralTag::kString ||
        entries[i + 1].tag != abc::LiteralTag::kInteger)
      return Fail(BodyEmitError::kConstant, instruction);
    const auto slot = std::get<abc::LiteralBits>(entries[i + 1].value).value;
    if (slot > UINT32_MAX) return Fail(BodyEmitError::kConstant, instruction);
    shape.largest_slot =
        shape.largest_slot
            ? std::max(*shape.largest_slot, static_cast<std::uint32_t>(slot))
            : static_cast<std::uint32_t>(slot);
  }
  return shape;
}
}  // namespace

std::variant<NamedScopes, BodyEmitFailure> ValidateNamedScopes(
    const abc::MethodBody& body, std::uint64_t frame_size,
    const MethodConstants* constants, std::size_t bytes,
    std::uint64_t work_limit) {
  try {
    std::size_t count = 0;
    for (const auto& instruction : body.instructions) {
      const auto lexical =
          analysis::LiftLexicalInstruction(instruction.decoded, frame_size);
      const auto* push = std::get_if<analysis::PushEnvironment>(&lexical);
      if (push && push->scope) ++count;
    }
    NamedScopes result;
    if (!count) return result;
    ScopeQuota quota{work_limit};
    if (!quota.Take(body.instructions.size()))
      return Fail(BodyEmitError::kWorkLimit, {});
    if (!constants)
      return Fail(BodyEmitError::kMissingConstants,
                  body.instructions.front().offset);
    if (body.instructions.size() > bytes ||
        count > (bytes - body.instructions.size()) / sizeof(ScopeSite))
      return Fail(BodyEmitError::kResourceLimit, {});
    result.admitted.resize(body.instructions.size());
    std::vector<ScopeSite> sites;
    sites.reserve(count);
    const auto retained = result.AllocatedBytes();
    const auto scratch = sites.capacity() * sizeof(ScopeSite);
    if (retained > bytes || scratch > bytes - retained)
      return Fail(BodyEmitError::kResourceLimit, {});
    const auto base_bytes = retained + scratch;
    const auto reference_work =
        std::max(1, std::bit_width(constants->index.regions.size()));
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto lexical = analysis::LiftLexicalInstruction(
          body.instructions[i].decoded, frame_size);
      const auto* push = std::get_if<analysis::PushEnvironment>(&lexical);
      if (!push || !push->scope) continue;
      if (!quota.Take(reference_work))
        return Fail(BodyEmitError::kWorkLimit, body.instructions[i].offset);
      const auto resolved = abc::ResolvePoolReference(
          constants->file, constants->header, constants->index,
          constants->method, *push->scope, abc::PoolEntityKind::kLiteralArray);
      if (const auto* failure = std::get_if<abc::MetadataError>(&resolved))
        return Fail(BodyEmitError::kConstant, body.instructions[i].offset,
                    *failure);
      sites.push_back({i, std::get<abc::PoolReference>(resolved).offset,
                       push->slots});
    }
    const auto levels = std::bit_width(sites.size());
    const auto sort_unit = kSortWorkPerLevel * levels;
    if (sites.size() > UINT64_MAX / sort_unit ||
        !quota.Take(sites.size() * sort_unit))
      return Fail(BodyEmitError::kWorkLimit, {});
    std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) {
      return a.literal.value < b.literal.value;
    });
    std::size_t name_count = 0;
    for (std::size_t start = 0; start < sites.size();) {
      std::size_t end = start + 1;
      while (end < sites.size() &&
             sites[end].literal == sites[start].literal)
        ++end;
      const auto instruction =
          body.instructions[sites[start].instruction].offset;
      const auto decoded = InspectLiteral(
          *constants, sites[start].literal, instruction, bytes - base_bytes,
          quota);
      if (const auto* failure = std::get_if<BodyEmitFailure>(&decoded))
        return *failure;
      const auto shape = std::get<ScopeShape>(decoded);
      if (shape.names > (bytes - base_bytes) / sizeof(abc::EntityOffset) -
                            name_count)
        return Fail(BodyEmitError::kResourceLimit, instruction);
      name_count += shape.names;
      for (std::size_t i = start; i < end; ++i) {
        if (shape.largest_slot && *shape.largest_slot >= sites[i].slots)
          return Fail(BodyEmitError::kConstant,
                      body.instructions[sites[i].instruction].offset);
        result.admitted[sites[i].instruction] = 1;
      }
      start = end;
    }
    std::vector<abc::EntityOffset> names(name_count);
    if (names.capacity() > (bytes - base_bytes) / sizeof(abc::EntityOffset))
      return Fail(BodyEmitError::kResourceLimit, {});
    const auto name_bytes = names.capacity() * sizeof(abc::EntityOffset);
    std::size_t cursor = 0;
    for (std::size_t start = 0; start < sites.size();) {
      std::size_t end = start + 1;
      while (end < sites.size() && sites[end].literal == sites[start].literal)
        ++end;
      const auto instruction =
          body.instructions[sites[start].instruction].offset;
      const auto read = ReadScopeLiteral(*constants, sites[start].literal,
                                         instruction,
                                         bytes - base_bytes - name_bytes,
                                         quota);
      if (const auto* failure = std::get_if<BodyEmitFailure>(&read))
        return *failure;
      const auto& entries = std::get<abc::LiteralArray>(read).entries;
      if (entries.empty() || entries.size() % kScopePairEntries != 1 ||
          entries.front().tag != abc::LiteralTag::kInteger ||
          (entries.size() - 1) / kScopePairEntries > names.size() - cursor)
        return Fail(BodyEmitError::kConstant, instruction);
      for (std::size_t i = 1; i < entries.size(); i += kScopePairEntries) {
        if (entries[i].tag != abc::LiteralTag::kString ||
            entries[i + 1].tag != abc::LiteralTag::kInteger)
          return Fail(BodyEmitError::kConstant, instruction);
        names[cursor++] = std::get<abc::EntityOffset>(entries[i].value);
      }
      start = end;
    }
    if (cursor != names.size()) return Fail(BodyEmitError::kConstant, {});
    const auto name_levels = std::bit_width(names.size());
    if (name_levels &&
        (names.size() > UINT64_MAX / (kSortWorkPerLevel * name_levels) ||
         !quota.Take(names.size() * kSortWorkPerLevel * name_levels)))
      return Fail(BodyEmitError::kWorkLimit, {});
    std::sort(names.begin(), names.end(), [](auto a, auto b) {
      return a.value < b.value;
    });
    names.erase(std::unique(names.begin(), names.end()), names.end());
    const abc::StringBudget strings{
        std::min(constants->string_budget.bytes,
                 bytes - base_bytes - name_bytes)};
    for (const auto name : names) {
      const auto valid = abc::ReadStringEntity(
          constants->file, constants->header, name, strings);
      if (const auto* failure = std::get_if<abc::MetadataError>(&valid))
        return Fail(*failure == abc::MetadataError::kResourceLimit
                        ? BodyEmitError::kResourceLimit
                        : BodyEmitError::kConstant,
                    {}, *failure);
      if (!quota.Take(std::get<std::u16string>(valid).size() + 1))
        return Fail(BodyEmitError::kWorkLimit, {});
    }
    result.work_units = quota.Used();
    return result;
  } catch (const std::bad_alloc&) {
    return Fail(BodyEmitError::kResourceLimit, {});
  } catch (const std::length_error&) {
    return Fail(BodyEmitError::kResourceLimit, {});
  }
}
}  // namespace abcd::source
