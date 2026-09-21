#ifndef ABCD_SRC_ABC_METADATA_CURSOR_H_
#define ABCD_SRC_ABC_METADATA_CURSOR_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "abcd/abc/metadata_index.h"

namespace abcd::corpus {
class AbcFile;
}

namespace abcd::abc {

// A small positional window keeps metadata reads bounded independently of the
// ABC size. Cursor errors retain their semantic category for the indexer.
class MetadataCursor {
 public:
  MetadataCursor(const corpus::AbcFile& file, std::uint32_t offset,
                 std::uint32_t limit);
  std::uint32_t Position() const;
  std::variant<std::uint8_t, MetadataError> U8();
  std::variant<std::uint16_t, MetadataError> U16();
  std::variant<std::uint32_t, MetadataError> U32();
  std::variant<std::uint32_t, MetadataError> Uleb();
  std::optional<MetadataError> SkipSleb();
  bool Skip(std::uint64_t count);

 private:
  static constexpr std::size_t kWindowBytes = 4096;
  const corpus::AbcFile& file_;
  std::uint32_t position_;
  std::uint32_t limit_;
  std::uint32_t buffer_start_ = 0;
  std::uint32_t buffer_end_ = 0;
  std::array<std::uint8_t, kWindowBytes> buffer_{};
};

std::variant<std::string, MetadataError> ReadMetadataString(
    MetadataCursor* cursor, std::uint32_t* utf16_length,
    std::size_t* spent, std::size_t budget);

}  // namespace abcd::abc

#endif  // ABCD_SRC_ABC_METADATA_CURSOR_H_
