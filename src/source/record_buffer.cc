#include "record_buffer.h"

#include <algorithm>
#include <bit>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string_view>

namespace abcd::source {
namespace {
constexpr std::size_t kPairEntries = 2;

class BufferReader {
 public:
  BufferReader(const corpus::AbcFile& file, const abc::Header& header,
               const abc::MetadataIndex& index, abc::EntityOffset method,
               std::size_t bytes, abc::StringBudget strings)
      : file_(file), header_(header), index_(index), method_(method),
        bytes_(bytes), strings_(strings) {}

  RecordBufferResult Read(abc::PoolIndex slot) {
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, method_, slot,
        abc::PoolEntityKind::kLiteralArray);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      return *error;
    const auto decoded = abc::ReadLiteralArray(
        file_, header_, std::get<abc::PoolReference>(reference).offset,
        {bytes_});
    if (const auto* error = std::get_if<abc::LiteralFailure>(&decoded))
      return *error;
    const auto& entries = std::get<abc::LiteralArray>(decoded).entries;
    if (entries.size() % kPairEntries != 0)
      return RecordBufferError::kShape;
    if (!Charge(entries.capacity(), sizeof(abc::LiteralEntry)))
      return RecordBufferError::kResourceLimit;
    const auto count = entries.size() / kPairEntries;
    if (count > Remaining() / sizeof(RecordLiteralField))
      return RecordBufferError::kResourceLimit;
    result_.fields.reserve(count);
    if (!Charge(result_.fields.capacity(), sizeof(RecordLiteralField)))
      return RecordBufferError::kResourceLimit;
    result_.retained_bytes +=
        result_.fields.capacity() * sizeof(RecordLiteralField);
    for (std::size_t i = 0; i < count; ++i) {
      const auto& key = entries[i * kPairEntries];
      const auto& value = entries[i * kPairEntries + 1];
      if (key.tag != abc::LiteralTag::kString ||
          !std::holds_alternative<abc::EntityOffset>(key.value))
        return RecordBufferError::kKey;
      auto name = ReadString(std::get<abc::EntityOffset>(key.value));
      if (const auto* error = std::get_if<abc::MetadataError>(&name))
        return *error;
      auto text = std::get<std::u16string>(std::move(name));
      if (text == u"__proto__") return RecordBufferError::kKey;
      if (!ChargeString(text)) return RecordBufferError::kResourceLimit;
      result_.retained_bytes += (text.capacity() + 1) * sizeof(char16_t);
      auto atom = ReadAtom(value);
      if (const auto* error = std::get_if<abc::MetadataError>(&atom))
        return *error;
      if (const auto* error = std::get_if<RecordBufferError>(&atom))
        return *error;
      auto data = std::get<StaticAtom>(std::move(atom));
      if (const auto* value_text = std::get_if<std::u16string>(&data)) {
        if (!ChargeString(*value_text))
          return RecordBufferError::kResourceLimit;
        result_.retained_bytes +=
            (value_text->capacity() + 1) * sizeof(char16_t);
      }
      result_.fields.push_back({std::move(text), std::move(data)});
    }
    if (count > Remaining() / sizeof(std::size_t))
      return RecordBufferError::kResourceLimit;
    std::vector<std::size_t> order;
    order.reserve(count);
    if (!Charge(order.capacity(), sizeof(std::size_t)))
      return RecordBufferError::kResourceLimit;
    order.resize(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t left,
                                              std::size_t right) {
      return result_.fields[left].key < result_.fields[right].key;
    });
    for (std::size_t i = 1; i < order.size(); ++i)
      if (result_.fields[order[i - 1]].key == result_.fields[order[i]].key)
        return RecordBufferError::kDuplicateKey;
    return std::move(result_);
  }

 private:
  bool Charge(std::size_t count, std::size_t width) {
    if (count > Remaining() / width) return false;
    charged_ += count * width;
    return true;
  }
  bool ChargeString(const std::u16string& value) {
    if (value.capacity() >= Remaining() / sizeof(char16_t)) return false;
    return Charge(value.capacity() + 1, sizeof(char16_t));
  }
  std::size_t Remaining() const { return bytes_ - charged_; }
  std::variant<std::u16string, abc::MetadataError> ReadString(
      abc::EntityOffset offset) const {
    return abc::ReadStringEntity(
        file_, header_, offset, {std::min(strings_.bytes, Remaining())});
  }
  std::variant<StaticAtom, abc::MetadataError, RecordBufferError> ReadAtom(
      const abc::LiteralEntry& entry) const {
    if (entry.tag == abc::LiteralTag::kString) {
      if (!std::holds_alternative<abc::EntityOffset>(entry.value))
        return RecordBufferError::kValue;
      auto result = ReadString(std::get<abc::EntityOffset>(entry.value));
      if (const auto* error = std::get_if<abc::MetadataError>(&result))
        return *error;
      return StaticAtom{std::get<std::u16string>(std::move(result))};
    }
    const auto* bits = std::get_if<abc::LiteralBits>(&entry.value);
    if (!bits) return RecordBufferError::kValue;
    switch (entry.tag) {
      case abc::LiteralTag::kNull:
        return StaticAtom{StaticNull{}};
      case abc::LiteralTag::kBoolean:
        if (bits->value > 1) return RecordBufferError::kValue;
        return StaticAtom{bits->value == 1};
      case abc::LiteralTag::kInteger:
        if (bits->value > UINT32_MAX) return RecordBufferError::kValue;
        return StaticAtom{std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(bits->value))};
      case abc::LiteralTag::kDouble:
        return StaticAtom{StaticFloat{bits->value}};
      default:
        return RecordBufferError::kValue;
    }
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  abc::EntityOffset method_;
  std::size_t bytes_;
  abc::StringBudget strings_;
  std::size_t charged_ = 0;
  BufferedRecordObject result_;
};
}  // namespace

RecordBufferResult ReadRecordObjectBuffer(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, abc::EntityOffset method,
    abc::PoolIndex literal, std::size_t bytes, abc::StringBudget strings) {
  try {
    return BufferReader(file, header, index, method, bytes, strings)
        .Read(literal);
  } catch (const std::bad_alloc&) {
    return RecordBufferError::kResourceLimit;
  } catch (const std::length_error&) {
    return RecordBufferError::kResourceLimit;
  }
}
}  // namespace abcd::source
