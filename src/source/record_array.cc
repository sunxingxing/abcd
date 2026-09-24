#include "record_array.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <utility>

namespace abcd::source {
namespace {
class ArrayReader {
 public:
  ArrayReader(const corpus::AbcFile& file, const abc::Header& header,
              const abc::MetadataIndex& index, abc::EntityOffset method,
              std::size_t bytes, abc::StringBudget strings)
      : file_(file), header_(header), index_(index), method_(method),
        bytes_(bytes), strings_(strings) {}

  BufferedArrayResult Read(const abc::DecodedInstruction& instruction) {
    std::uint64_t cache_max;
    switch (instruction.opcode) {
      case abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16:
        cache_max = UINT8_MAX;
        break;
      case abc::Opcode::kCREATEARRAYWITHBUFFER_IMM16_ID16:
        cache_max = UINT16_MAX;
        break;
      default:
        return BufferedArrayError::kInstruction;
    }
    if (instruction.operand_count != 2 ||
        instruction.operands[0].kind != abc::OperandKind::kImmediate ||
        instruction.operands[0].raw_bits > cache_max ||
        instruction.operands[1].kind != abc::OperandKind::kLiteralArray ||
        instruction.operands[1].raw_bits > UINT16_MAX)
      return BufferedArrayError::kInstruction;
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, method_,
        {static_cast<std::uint32_t>(instruction.operands[1].raw_bits)},
        abc::PoolEntityKind::kLiteralArray);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      return *error;
    const auto decoded = abc::ReadLiteralArray(
        file_, header_, std::get<abc::PoolReference>(reference).offset,
        {bytes_});
    if (const auto* error = std::get_if<abc::LiteralFailure>(&decoded))
      return *error;
    const auto& entries = std::get<abc::LiteralArray>(decoded).entries;
    if (!Charge(entries.capacity(), sizeof(abc::LiteralEntry)) ||
        entries.size() > Remaining() / sizeof(StaticAtom))
      return BufferedArrayError::kResourceLimit;
    result_.elements.reserve(entries.size());
    if (!Charge(result_.elements.capacity(), sizeof(StaticAtom)))
      return BufferedArrayError::kResourceLimit;
    result_.retained_bytes = result_.elements.capacity() * sizeof(StaticAtom);
    for (const auto& entry : entries) {
      auto atom = Element(entry);
      if (const auto* error = std::get_if<abc::MetadataError>(&atom))
        return *error;
      if (const auto* error = std::get_if<BufferedArrayError>(&atom))
        return *error;
      result_.elements.push_back(std::get<StaticAtom>(std::move(atom)));
      if (const auto* text =
              std::get_if<std::u16string>(&result_.elements.back())) {
        if (!Charge(text->capacity(), sizeof(char16_t)))
          return BufferedArrayError::kResourceLimit;
        result_.retained_bytes += text->capacity() * sizeof(char16_t);
      }
    }
    return std::move(result_);
  }

 private:
  bool Charge(std::size_t count, std::size_t width) {
    if (count > Remaining() / width) return false;
    charged_ += count * width;
    return true;
  }
  std::size_t Remaining() const { return bytes_ - charged_; }
  std::variant<StaticAtom, BufferedArrayError, abc::MetadataError> Element(
      const abc::LiteralEntry& entry) const {
    if (entry.tag == abc::LiteralTag::kString) {
      const auto* offset = std::get_if<abc::EntityOffset>(&entry.value);
      if (!offset) return BufferedArrayError::kValue;
      auto text = abc::ReadStringEntity(
          file_, header_, *offset,
          {std::min(strings_.bytes, Remaining())});
      if (const auto* error = std::get_if<abc::MetadataError>(&text))
        return *error;
      return StaticAtom{std::get<std::u16string>(std::move(text))};
    }
    const auto* bits = std::get_if<abc::LiteralBits>(&entry.value);
    if (!bits) return BufferedArrayError::kValue;
    switch (entry.tag) {
      case abc::LiteralTag::kNull:
        return StaticAtom{StaticNull{}};
      case abc::LiteralTag::kBoolean:
        if (bits->value > 1) return BufferedArrayError::kValue;
        return StaticAtom{bits->value == 1};
      case abc::LiteralTag::kInteger:
        if (bits->value > UINT32_MAX) return BufferedArrayError::kValue;
        return StaticAtom{std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(bits->value))};
      case abc::LiteralTag::kFloat: {
        if (bits->value > UINT32_MAX) return BufferedArrayError::kValue;
        const auto value = std::bit_cast<float>(
            static_cast<std::uint32_t>(bits->value));
        return StaticAtom{StaticFloat{std::bit_cast<std::uint64_t>(
            static_cast<double>(value))}};
      }
      case abc::LiteralTag::kDouble:
        return StaticAtom{StaticFloat{bits->value}};
      default:
        return BufferedArrayError::kValue;
    }
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  abc::EntityOffset method_;
  std::size_t bytes_;
  abc::StringBudget strings_;
  std::size_t charged_ = 0;
  BufferedArray result_;
};
}  // namespace

BufferedArrayResult ReadBufferedArray(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, abc::EntityOffset method,
    const abc::DecodedInstruction& instruction, std::size_t bytes,
    abc::StringBudget strings) {
  try {
    return ArrayReader(file, header, index, method, bytes, strings)
        .Read(instruction);
  } catch (const std::bad_alloc&) {
    return BufferedArrayError::kResourceLimit;
  } catch (const std::length_error&) {
    return BufferedArrayError::kResourceLimit;
  }
}
}  // namespace abcd::source
