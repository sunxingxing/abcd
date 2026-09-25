#include "object_buffer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <numeric>
#include <new>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "abcd/abc/pool_reference.h"
#include "abcd/source/string_literal.h"
#include "member_key.h"

namespace abcd::source {
namespace {
constexpr std::size_t kPairEntries = 2;

bool DataValue(const abc::LiteralEntry& entry) {
  if (entry.tag == abc::LiteralTag::kString)
    return std::holds_alternative<abc::EntityOffset>(entry.value);
  const auto* bits = std::get_if<abc::LiteralBits>(&entry.value);
  if (!bits) return false;
  switch (entry.tag) {
    case abc::LiteralTag::kNull:
    case abc::LiteralTag::kDouble:
      return true;
    case abc::LiteralTag::kBoolean:
      return bits->value <= 1;
    case abc::LiteralTag::kInteger:
      return bits->value <= UINT32_MAX;
    default:
      return false;
  }
}

class ObjectWriter {
 public:
  ObjectWriter(std::ostream& output, EmitBudget budget, abc::CodeOffset offset)
      : output_(output), remaining_(budget.bytes), offset_(offset) {}

  BodyEmitResult Write(const abc::LiteralArray& literal,
                       const std::vector<std::u16string>& keys,
                       const MethodConstants& constants,
                       std::size_t string_bytes,
                       std::optional<std::size_t> array_field) {
    try {
      Text("({");
      for (std::size_t i = 0; i < keys.size() && !error_; ++i) {
        if (i) Text(", ");
        switch (ClassifyObjectKey(keys[i])) {
          case MemberKeySyntax::kIdentifier:
            for (const auto unit : keys[i]) {
              const auto ch = static_cast<char>(unit);
              Text({&ch, 1});
            }
            break;
          case MemberKeySyntax::kQuoted:
            Text("[");
            String(keys[i]);
            Text("]");
            break;
        }
        Text(": ");
        if (array_field == i) Text("[]");
        else Value(literal.entries[i * kPairEntries + 1], constants, string_bytes);
      }
      Text("})");
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return Failure(BodyEmitError::kExpression, EmitError::kWrite);
    }
  }

 private:
  BodyEmitFailure Failure(BodyEmitError kind,
                          std::optional<EmitError> emit = {}) const {
    return {kind, offset_, {}, emit};
  }
  void Text(std::string_view text) {
    if (error_) return;
    if (text.size() > remaining_) {
      error_ = Failure(BodyEmitError::kExpression, EmitError::kOutputLimit);
      return;
    }
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) {
      error_ = Failure(BodyEmitError::kExpression, EmitError::kWrite);
      return;
    }
    remaining_ -= text.size();
    written_ += text.size();
  }
  void Account(const std::variant<std::size_t, EmitError>& result) {
    if (const auto* error = std::get_if<EmitError>(&result)) {
      error_ = Failure(BodyEmitError::kExpression, *error);
      return;
    }
    const auto bytes = std::get<std::size_t>(result);
    remaining_ -= bytes;
    written_ += bytes;
  }
  void String(std::u16string_view value) {
    if (!error_) Account(WriteStringLiteral(value, output_, {remaining_}));
  }
  void Value(const abc::LiteralEntry& value,
             const MethodConstants& constants, std::size_t string_bytes) {
    if (error_) return;
    if (value.tag == abc::LiteralTag::kString) {
      const auto text = abc::ReadStringEntity(
          constants.file, constants.header,
          std::get<abc::EntityOffset>(value.value),
          {std::min(constants.string_budget.bytes, string_bytes)});
      if (const auto* error = std::get_if<abc::MetadataError>(&text)) {
        error_ = Failure(*error == abc::MetadataError::kResourceLimit
                             ? BodyEmitError::kResourceLimit
                             : BodyEmitError::kConstant);
        error_->metadata_error = *error;
      } else {
        String(std::get<std::u16string>(text));
      }
      return;
    }
    const auto bits = std::get<abc::LiteralBits>(value.value).value;
    switch (value.tag) {
      case abc::LiteralTag::kNull:
        Text("null");
        break;
      case abc::LiteralTag::kBoolean:
        Text(bits == 0 ? "false" : "true");
        break;
      case abc::LiteralTag::kInteger: {
        std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1>
            buffer{};
        const auto end = std::to_chars(buffer.data(), buffer.data() +
                                          buffer.size(),
                                      static_cast<std::uint32_t>(bits));
        Text(std::string_view(buffer.data(), end.ptr - buffer.data()));
        break;
      }
      case abc::LiteralTag::kDouble:
        Account(WriteScalarExpression(
            analysis::ScalarValue{analysis::Float64Bits{bits}}, {"", {}},
            output_, {remaining_}));
        break;
      default:
        error_ = Failure(BodyEmitError::kConstant);
        error_->object_error = ObjectBufferError::kValue;
        break;
    }
  }
  std::ostream& output_;
  std::size_t remaining_;
  abc::CodeOffset offset_;
  std::size_t written_ = 0;
  std::optional<BodyEmitFailure> error_;
};
}  // namespace

ObjectBufferResult ReadObjectBuffer(const analysis::BufferedObjectCreation& creation,
                                 abc::CodeOffset offset,
                                 const MethodConstants& constants,
                                 ObjectBufferBudget scratch_budget) {
  const auto fail = [&](ObjectBufferError error) -> ObjectBufferResult {
    BodyEmitFailure failure{
        error == ObjectBufferError::kResourceLimit
            ? BodyEmitError::kResourceLimit
            : BodyEmitError::kConstant,
        offset, {}, {}};
    failure.object_error = error;
    return failure;
  };
  const auto reference = abc::ResolvePoolReference(
      constants.file, constants.header, constants.index, constants.method,
      creation.literal, abc::PoolEntityKind::kLiteralArray);
  if (const auto* error = std::get_if<abc::MetadataError>(&reference)) {
    BodyEmitFailure failure{
        *error == abc::MetadataError::kResourceLimit
            ? BodyEmitError::kResourceLimit
            : BodyEmitError::kConstant,
        offset, {}, {}};
    failure.metadata_error = *error;
    return failure;
  }
  auto read = abc::ReadLiteralArray(
      constants.file, constants.header,
      std::get<abc::PoolReference>(reference).offset,
      {scratch_budget.bytes});
  if (const auto* error = std::get_if<abc::LiteralFailure>(&read)) {
    BodyEmitFailure failure{
        error->kind == abc::LiteralError::kResourceLimit
            ? BodyEmitError::kResourceLimit
            : BodyEmitError::kConstant,
        offset, {}, {}};
    failure.literal_error = *error;
    return failure;
  }
  const auto& literal = std::get<abc::LiteralArray>(read);
  if (literal.entries.size() % kPairEntries != 0)
    return fail(ObjectBufferError::kShape);
  const auto count = literal.entries.size() / kPairEntries;
  std::size_t remaining = scratch_budget.bytes;
  const auto charge = [&](std::size_t items, std::size_t width) {
    if (items > remaining / width) return false;
    remaining -= items * width;
    return true;
  };
  if (!charge(literal.entries.capacity(), sizeof(abc::LiteralEntry)))
    return fail(ObjectBufferError::kResourceLimit);
  try {
    if (count > remaining / sizeof(std::u16string))
      return fail(ObjectBufferError::kResourceLimit);
    std::vector<std::u16string> keys;
    keys.reserve(count);
    if (!charge(keys.capacity(), sizeof(std::u16string)))
      return fail(ObjectBufferError::kResourceLimit);
    for (std::size_t i = 0; i < count; ++i) {
      const auto& key = literal.entries[i * kPairEntries];
      const auto& value = literal.entries[i * kPairEntries + 1];
      if (key.tag != abc::LiteralTag::kString ||
          !std::holds_alternative<abc::EntityOffset>(key.value))
        return fail(ObjectBufferError::kKey);
      if (!DataValue(value)) return fail(ObjectBufferError::kValue);
      auto name = abc::ReadStringEntity(
          constants.file, constants.header,
          std::get<abc::EntityOffset>(key.value),
          {std::min(constants.string_budget.bytes, remaining)});
      if (const auto* error = std::get_if<abc::MetadataError>(&name)) {
        BodyEmitFailure failure{
            *error == abc::MetadataError::kResourceLimit
                ? BodyEmitError::kResourceLimit
                : BodyEmitError::kConstant,
            offset, {}, {}};
        failure.metadata_error = *error;
        return failure;
      }
      auto text = std::move(std::get<std::u16string>(name));
      // The retained UTF-16 allocation includes its terminator.
      if (text.capacity() >= remaining / sizeof(char16_t) ||
          !charge(text.capacity() + 1, sizeof(char16_t)))
        return fail(ObjectBufferError::kResourceLimit);
      keys.push_back(std::move(text));
    }
    if (count > remaining / sizeof(std::size_t))
      return fail(ObjectBufferError::kResourceLimit);
    std::vector<std::size_t> order(count);
    if (!charge(order.capacity(), sizeof(std::size_t)))
      return fail(ObjectBufferError::kResourceLimit);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      return keys[a] < keys[b];
    });
    for (std::size_t i = 1; i < count; ++i)
      if (keys[order[i - 1]] == keys[order[i]])
        return fail(ObjectBufferError::kDuplicateKey);
    return ObjectBuffer{std::move(std::get<abc::LiteralArray>(read)),
                        std::move(keys), std::move(order),
                        scratch_budget.bytes - remaining};
  } catch (const std::bad_alloc&) {
    return fail(ObjectBufferError::kResourceLimit);
  } catch (const std::length_error&) {
    return fail(ObjectBufferError::kResourceLimit);
  }
}
BodyEmitResult WriteObjectBuffer(const analysis::BufferedObjectCreation& creation,
                                 abc::CodeOffset offset,
                                 const MethodConstants& constants,
                                 std::ostream& output, EmitBudget output_budget,
                                 ObjectBufferBudget scratch_budget,
                                 std::optional<ObjectField> field) {
  const auto parsed = ReadObjectBuffer(creation, offset, constants, scratch_budget);
  if (const auto* failure = std::get_if<BodyEmitFailure>(&parsed)) return *failure;
  const auto& buffer = std::get<ObjectBuffer>(parsed);
  const auto metadata_failure = [&](abc::MetadataError error) {
    BodyEmitFailure failure{error == abc::MetadataError::kResourceLimit
        ? BodyEmitError::kResourceLimit : BodyEmitError::kConstant, offset};
    failure.metadata_error = error;
    return failure;
  };
  std::optional<std::size_t> array_field;
  if (field) {
    if (!std::holds_alternative<analysis::EmptyArray>(field->value))
      return BodyEmitFailure{BodyEmitError::kConstant, offset};
    const auto resolved = abc::ResolvePoolReference(constants.file, constants.header,
        constants.index, constants.method, field->name, abc::PoolEntityKind::kString);
    const auto* reference = std::get_if<abc::PoolReference>(&resolved);
    if (!reference) return metadata_failure(std::get<abc::MetadataError>(resolved));
    const auto read = abc::ReadStringEntity(constants.file, constants.header,
        reference->offset, {std::min(constants.string_budget.bytes,
            scratch_budget.bytes - buffer.allocated_bytes)});
    const auto* name = std::get_if<std::u16string>(&read);
    if (!name) return metadata_failure(std::get<abc::MetadataError>(read));
    const auto found = std::lower_bound(buffer.order.begin(), buffer.order.end(), *name,
        [&](std::size_t index, const std::u16string& key) { return buffer.keys[index] < key; });
    if (found == buffer.order.end() || buffer.keys[*found] != *name)
      return BodyEmitFailure{BodyEmitError::kConstant, offset};
    array_field = *found;
  }
  return ObjectWriter(output, output_budget, offset).Write(
      buffer.literal, buffer.keys, constants,
      scratch_budget.bytes - buffer.allocated_bytes, array_field);
}
}  // namespace abcd::source
