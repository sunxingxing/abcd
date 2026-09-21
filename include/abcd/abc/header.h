#ifndef ABCD_ABC_HEADER_H_
#define ABCD_ABC_HEADER_H_

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace abcd::abc {

// Source: OpenHarmony-v7.0-Release runtime_core 3e147bb, libpandafile/file.h
// and static_core/libarkfile/file.h. Recognition is not semantic certification.
constexpr std::size_t kDynamicHeaderSize = 60;
constexpr std::size_t kStaticHeaderSize = 72;
constexpr std::size_t kMaxHeaderSize = kStaticHeaderSize;

struct AbcVersion {
  std::array<std::uint8_t, 4> bytes{};

  constexpr auto operator<=>(const AbcVersion&) const = default;
};

enum class AbcKind {
  kDynamic,
  kStatic,
  kUnknown,
};

enum class ProfileId {
  kUnknown,
  kDynamic9,
  kDynamic1102,
  kDynamic1202,
  kDynamic1206,
  kDynamic1301,
  kDynamic2400,
  kStatic0107,
};

struct IndexSection {
  std::uint32_t count = 0;
  std::uint32_t offset = 0;
};

struct DynamicLayout {
  IndexSection classes;
  IndexSection line_number_programs;
  IndexSection literal_arrays;
  IndexSection index_regions;
};

struct StaticLayout {
  std::uint32_t quickened_flag = 0;
  IndexSection classes;
  IndexSection export_table;
  IndexSection line_number_programs;
  IndexSection literal_arrays;
  IndexSection index_regions;
};

// Common fields were read successfully, but the layout is intentionally opaque.
struct UnknownLayout {};

struct Header {
  std::array<std::uint8_t, 8> magic{};
  std::uint32_t checksum = 0;
  AbcVersion version;
  std::uint32_t file_size = 0;
  std::uint32_t foreign_offset = 0;
  std::uint32_t foreign_size = 0;
  AbcKind kind = AbcKind::kUnknown;
  ProfileId profile = ProfileId::kUnknown;
  std::variant<DynamicLayout, StaticLayout, UnknownLayout> layout;
};

enum class HeaderError {
  kTruncated,
  kBadMagic,
  kActualSizeOverflow,
  kFileSizeMismatch,
  kForeignRange,
  kIncompatibleVersion,
  kStaticCandidateVersion,
  kIndexAlignment,
  kIndexRange,
  kReservedLiteralFields,
};

using HeaderResult = std::variant<Header, HeaderError>;

[[nodiscard]] HeaderResult ParseHeader(std::span<const std::uint8_t> prefix,
                                       std::uint64_t actual_size);

// Verifies only the evidenced 72-byte static layout. Version 0.0.0.6 remains
// unknown until an external language witness establishes its semantic kind.
[[nodiscard]] HeaderResult ParseStaticHeader(
    std::span<const std::uint8_t> prefix, std::uint64_t actual_size);

}  // namespace abcd::abc

#endif  // ABCD_ABC_HEADER_H_
