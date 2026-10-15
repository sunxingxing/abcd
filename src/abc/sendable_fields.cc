#include "abcd/abc/sendable_fields.h"
#include <new>
#include <stdexcept>
namespace abcd::abc {
std::variant<SendableFields, SendableFieldFailure> DecodeSendableFields(
    const LiteralArray& input, SendableFieldBudget budget) {
  using Error = SendableFieldError;
  const auto initial = budget;
  std::optional<std::uint32_t> field;
  try {
    const auto require = [&](bool ok, Error kind) {
      if (!ok) throw SendableFieldFailure{kind, field};
    };
    const auto step = [&] {
      require(budget.work != 0, Error::kWorkLimit); --budget.work;
    };
    step();
    const auto& entries = input.entries;
    constexpr std::size_t kPair = 2;
    require(!entries.empty() && entries.size() % kPair == 1, Error::kShape);
    require(entries.back().tag == LiteralTag::kInteger, Error::kSplit);
    const auto* count = std::get_if<LiteralBits>(&entries.back().value);
    const auto size = (entries.size() - 1) / kPair;
    require(count && count->value <= size && size <= UINT32_MAX, Error::kSplit);
    require(sizeof(SendableFields) <= budget.bytes, Error::kResourceLimit);
    budget.bytes -= sizeof(SendableFields);
    require(size <= budget.bytes / sizeof(SendableField), Error::kResourceLimit);
    budget.bytes -= size * sizeof(SendableField);
    SendableFields result;
    result.instance_count = static_cast<std::uint32_t>(count->value);
    result.fields.reserve(size);
    require(result.fields.capacity() - size <= budget.bytes / sizeof(SendableField), Error::kResourceLimit);
    budget.bytes -= (result.fields.capacity() - size) * sizeof(SendableField);
    for (std::size_t i = 0; i < size; ++i) {
      field = static_cast<std::uint32_t>(i); step();
      const auto& key = entries[i * kPair];
      const auto* name = std::get_if<EntityOffset>(&key.value);
      require(key.tag == LiteralTag::kString && name && name->value >= kDynamicHeaderSize, Error::kKey);
      const auto& mask = entries[i * kPair + 1];
      const auto* bits = std::get_if<LiteralBits>(&mask.value);
      require(mask.tag == LiteralTag::kInteger && bits && bits->value <= UINT8_MAX, Error::kMask);
      result.fields.push_back({key, {static_cast<std::uint8_t>(bits->value)},
          i < result.instance_count ? SendableFieldPlacement::kInstance : SendableFieldPlacement::kStatic});
    }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (const SendableFieldFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return SendableFieldFailure{Error::kResourceLimit, field};
  } catch (const std::length_error&) { return SendableFieldFailure{Error::kResourceLimit, field}; }
}
}
