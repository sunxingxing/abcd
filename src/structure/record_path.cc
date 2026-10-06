#include "abcd/structure/record_path.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <new>
#include <stdexcept>
#include <tuple>

#include "abcd/structure/source_identity.h"
#include "package_source.h"

namespace abcd::structure {
namespace {
enum class PathStyle { kArchive, kSource };
constexpr std::size_t kComponentBytes = 200, kPathBytes = 2048;
std::variant<RecordPath, PathError> EncodePath(std::string_view descriptor,
                                               abc::EntityOffset entity,
                                               PathStyle style) {
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string_view name = descriptor;
  if (name.size() < 3 || name.front() != 'L' || name.back() != ';')
    return PathError::kDescriptor;
  name.remove_prefix(1);
  name.remove_suffix(1);
  RecordPath path;
  std::size_t component = 0;
  std::size_t position = 0;
  std::size_t component_end = name.find('/');
  if (component_end == std::string_view::npos) component_end = name.size();
  for (const unsigned char byte : name) {
    if (byte == '/') {
      if (component == 0) path.stem += "%00";
      path.stem += '/';
      component = 0;
      component_end = name.find('/', position + 1);
      if (component_end == std::string_view::npos) component_end = name.size();
    } else {
      const bool plain =
          (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' ||
          (style == PathStyle::kSource && byte == '.' &&
           position + 1 < component_end);
      const auto count = plain ? 1u : 3u;
      if (count > kComponentBytes - component)
        return PathError::kComponentLimit;
      if (plain)
        path.stem += static_cast<char>(byte);
      else {
        path.stem += '%';
        path.stem += kHex[byte >> 4];
        path.stem += kHex[byte & 0xf];
      }
      component += count;
    }
    if (path.stem.size() > kPathBytes) return PathError::kPathLimit;
    ++position;
  }
  if (component == 0) path.stem += "%00";
  if (path.stem.size() > kPathBytes) return PathError::kPathLimit;
  if (style == PathStyle::kSource) return path;
  constexpr std::size_t kEntityDigits = sizeof(entity.value) * 2;
  std::array<char, kEntityDigits> digits;
  digits.fill('0');
  std::array<char, kEntityDigits> encoded{};
  const auto end =
      std::to_chars(encoded.data(), encoded.data() + encoded.size(),
                    entity.value, 16)
          .ptr;
  const auto size = static_cast<std::size_t>(end - encoded.data());
  std::copy(encoded.begin(), encoded.begin() + size, digits.end() - size);
  path.stem += "~r";
  path.stem.append(digits.data(), digits.size());
  if (path.stem.size() > kPathBytes) return PathError::kPathLimit;
  return path;
}
unsigned char Fold(unsigned char byte) {
  return byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
}
bool PathLess(const RecordPath* left, const RecordPath* right) {
  return std::lexicographical_compare(
      left->stem.begin(), left->stem.end(), right->stem.begin(),
      right->stem.end(),
      [](unsigned char a, unsigned char b) { return Fold(a) < Fold(b); });
}
struct SourcePathContext {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
};
// Unicode scalar conversion follows Unicode UTF-16 and RFC 3629. Filenames
// remain textual evidence: unsupported spelling falls back to the descriptor,
// never replacement-character normalization or an external filesystem lookup.
constexpr std::uint32_t kHighSurrogate = 0xd800, kLowSurrogate = 0xdc00,
                        kSurrogateEnd = 0xe000;
constexpr std::uint32_t kSupplementary = 0x10000;
constexpr unsigned kSurrogateBits = 10, kUtf8Bits = 6;
constexpr std::uint32_t kUtf8Mask = 0x3f, kAsciiEnd = 0x80, kTwoByteEnd = 0x800;
constexpr std::uint8_t kUtf8Continuation = 0x80, kUtf8Two = 0xc0,
                       kUtf8Three = 0xe0, kUtf8Four = 0xf0;
std::optional<std::uint32_t> Scalar(std::u16string_view text,
                                    std::size_t& index) {
  const std::uint32_t first = text[index++];
  if (first < kHighSurrogate || first >= kSurrogateEnd) return first;
  if (first >= kLowSurrogate || index == text.size()) return {};
  const std::uint32_t second = text[index++];
  if (second < kLowSurrogate || second >= kSurrogateEnd) return {};
  return kSupplementary + ((first - kHighSurrogate) << kSurrogateBits) +
         second - kLowSurrogate;
}
bool RelativeSource(std::u16string_view path) {
  if (path.empty()) return false;
  while (!path.empty()) {
    const auto end = path.find(u'/');
    const auto part = path.substr(0, end);
    if (part.empty() || part == u"." || part == u"..") return false;
    for (std::size_t index = 0; index < part.size();) {
      const auto scalar = Scalar(part, index);
      if (!scalar || *scalar < u' ' || *scalar == u'\\' || *scalar == u':' ||
          *scalar == u'|')
        return false;
    }
    if (end == std::u16string_view::npos) return true;
    path.remove_prefix(end + 1);
  }
  return false;
}
struct SourceName {
  std::u16string file;
  std::optional<PackageSource> package;
  bool entry_scope = false, version_scope = false;
};
void Charge(std::size_t count, std::size_t width, std::size_t& bytes) {
  if (count > bytes / width) throw PathError::kResourceLimit;
  bytes -= count * width;
}
// Reserve before parsing views into owned strings. Neither names nor their
// contents move afterwards. Sort once by the complete producer namespace;
// entry/version qualification expresses ambiguity, not recovery admission.
std::vector<SourceName> SourceNames(const SourcePathContext& context,
                                    std::size_t& bytes) {
  std::vector<SourceName> names;
  std::vector<SourceName*> order;
  const auto count = context.index.records.size();
  Charge(count, sizeof(SourceName) + sizeof(SourceName*), bytes);
  names.reserve(count);
  order.reserve(count);
  Charge(names.capacity() - count, sizeof(SourceName), bytes);
  Charge(order.capacity() - count, sizeof(SourceName*), bytes);
  names.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto read = ReadSourceIdentity(context.file, context.header, context.index,
                                   context.index.records[i]);
    if (std::holds_alternative<StructureFailure>(read))
      throw PathError::kSourceEvidence;
    auto& identity = std::get<SourceIdentity>(read);
    if (identity.state != SourceIdentityState::kUnique) continue;
    const auto parsed = ParsePackageSource(identity.file);
    if (!parsed) continue;
    const auto& source = *parsed;
    const auto dot = source.path.rfind(u'.');
    const auto suffix = dot == std::u16string_view::npos
                            ? std::u16string_view{}
                            : source.path.substr(dot);
    if (!RelativeSource(source.entry) || !RelativeSource(source.package) ||
        !RelativeSource(source.version) || !RelativeSource(source.path) ||
        !RelativeSource(source.path.substr(0, dot)) ||
        !(suffix == u".ts" || suffix == u".ets" || suffix == u".js" ||
          suffix == u".mjs" || suffix == u".cjs")) {
      continue;
    }
    Charge(identity.file.capacity() + 1, sizeof(char16_t), bytes);
    auto& name = names[i];
    name.file = std::move(identity.file);
    // Reparse after moving: short-string storage need not retain its address.
    name.package = ParsePackageSource(name.file);
    order.push_back(&name);
  }
  const auto key = [](const SourceName* name) {
    const auto& source = *name->package;
    return std::tie(source.entry, source.package, source.version);
  };
  std::sort(order.begin(), order.end(),
            [&](const auto* a, const auto* b) { return key(a) < key(b); });
  const bool entries = !order.empty() && order.front()->package->entry !=
                                             order.back()->package->entry;
  for (std::size_t first = 0; first < order.size();) {
    auto end = first + 1;
    while (end < order.size() &&
           order[end]->package->entry == order[first]->package->entry &&
           order[end]->package->package == order[first]->package->package)
      ++end;
    const bool versions =
        order[first]->package->version != order[end - 1]->package->version;
    for (auto i = first; i < end; ++i) {
      order[i]->entry_scope = entries;
      order[i]->version_scope = versions;
    }
    first = end;
  }
  return names;
}
std::variant<RecordPath, PathError> SourcePath(const abc::RecordInfo& record,
                                               const SourceName* name) {
  if (!name || !name->package)
    return EncodePath(record.descriptor, record.offset, PathStyle::kSource);
  // Conversion scratch is bounded by the existing output path limit. Percent
  // encoding stays in the common byte encoder, including traversal/case safety.
  std::array<char, kPathBytes + 2> buffer{};
  std::size_t used = 0;
  const auto byte = [&](std::uint8_t value) {
    if (used == buffer.size()) throw PathError::kPathLimit;
    buffer[used++] = static_cast<char>(value);
  };
  const auto text = [&](std::u16string_view value) {
    for (std::size_t index = 0; index < value.size();) {
      const auto decoded = Scalar(value, index);
      if (!decoded) throw PathError::kSourceEvidence;
      const auto scalar = *decoded;
      if (scalar < kAsciiEnd) {
        byte(scalar);
        continue;
      }
      const unsigned followers = scalar < kTwoByteEnd      ? 1
                                 : scalar < kSupplementary ? 2
                                                           : 3;
      const auto lead = followers == 1   ? kUtf8Two
                        : followers == 2 ? kUtf8Three
                                         : kUtf8Four;
      byte(lead | (scalar >> (followers * kUtf8Bits)));
      for (unsigned left = followers; left > 0; --left)
        byte(kUtf8Continuation |
             ((scalar >> ((left - 1) * kUtf8Bits)) & kUtf8Mask));
    }
  };
  byte('L');
  const auto& source = *name->package;
  if (name->entry_scope) {
    text(source.entry);
    byte('/');
  }
  text(source.package);
  byte('/');
  if (name->version_scope) {
    text(source.version);
    byte('/');
  }
  text(source.path.substr(0, source.path.rfind(u'.')));
  byte(';');
  auto result =
      EncodePath({buffer.data(), used}, record.offset, PathStyle::kSource);
  if (auto* path = std::get_if<RecordPath>(&result))
    path->origin = SourcePathOrigin::kPackageIdentity;
  return result;
}
}  // namespace

std::variant<RecordPath, PathError> PlanRecordPath(
    const abc::RecordInfo& record) {
  return EncodePath(record.descriptor, record.offset, PathStyle::kArchive);
}

static std::variant<std::vector<RecordPath>, PathError> PlanSourcePaths(
    std::span<const abc::RecordInfo> records, std::size_t budget_bytes,
    const SourcePathContext* context) {
  constexpr auto kRecordBytes = sizeof(RecordPath) + sizeof(const RecordPath*);
  if (records.size() > budget_bytes / kRecordBytes)
    return PathError::kResourceLimit;
  try {
    std::vector<RecordPath> paths;
    std::vector<const RecordPath*> order;
    paths.reserve(records.size());
    order.reserve(records.size());
    if (paths.capacity() > budget_bytes / sizeof(RecordPath))
      return PathError::kResourceLimit;
    budget_bytes -= paths.capacity() * sizeof(RecordPath);
    if (order.capacity() > budget_bytes / sizeof(const RecordPath*))
      return PathError::kResourceLimit;
    budget_bytes -= order.capacity() * sizeof(const RecordPath*);
    std::vector<SourceName> names;
    if (context) {
      Charge(sizeof(std::array<char, kPathBytes + 2>), 1, budget_bytes);
      names = SourceNames(*context, budget_bytes);
    }
    for (std::size_t i = 0; i < records.size(); ++i) {
      auto planned = SourcePath(records[i], context ? &names[i] : nullptr);
      if (const auto* error = std::get_if<PathError>(&planned)) return *error;
      auto path = std::get<RecordPath>(std::move(planned));
      if (path.stem.capacity() >= budget_bytes)
        return PathError::kResourceLimit;
      budget_bytes -= path.stem.capacity() + 1;
      paths.push_back(std::move(path));
      order.push_back(&paths.back());
    }
    // ASCII folding avoids silent overwrite on case-insensitive export hosts.
    // Percent escapes are injective; literal '%' is always escaped too.
    std::sort(order.begin(), order.end(), PathLess);
    for (std::size_t i = 1; i < order.size(); ++i)
      if (!PathLess(order[i - 1], order[i])) return PathError::kCollision;
    return paths;
  } catch (PathError error) {
    return error;
  } catch (const std::bad_alloc&) {
    return PathError::kResourceLimit;
  } catch (const std::length_error&) {
    return PathError::kResourceLimit;
  }
}
std::variant<std::vector<RecordPath>, PathError> PlanRecordSourcePaths(
    std::span<const abc::RecordInfo> records, std::size_t budget_bytes) {
  return PlanSourcePaths(records, budget_bytes, nullptr);
}
std::variant<std::vector<RecordPath>, PathError> PlanRecordSourcePaths(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, std::size_t budget_bytes) {
  const SourcePathContext context{file, header, index};
  return PlanSourcePaths(index.records, budget_bytes, &context);
}
}  // namespace abcd::structure
