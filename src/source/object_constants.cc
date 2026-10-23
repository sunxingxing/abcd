#include "object_constants.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
#include <new>
#include <stdexcept>
#include "object_buffer.h"

namespace abcd::source {
namespace {
using Error = TreeConstantError;
struct BufferUse { abc::PoolIndex slot; analysis::OpId operation; AggregateKind kind; };
constexpr std::size_t kPairEntries = 2; // v7 ExtractObjectDatas key/value pairs.
class Resolver {
 public:
  Resolver(TreeConstants& output, analysis::SemanticBudget budget) : out_(output), initial_(budget), left_(budget) {}
  void Step(std::uint64_t n = 1) { if (n > left_.work) throw Error::kWorkLimit; left_.work -= n; }
  void Admit(std::size_t n, std::size_t width) {
    if (n > left_.bytes / width) throw Error::kResourceLimit;
    left_.bytes -= n * width;
  }
  template<class T> void Grow(std::vector<T>& values, std::size_t count) {
    if (count <= values.capacity()) return;
    if (values.capacity() <= std::numeric_limits<std::size_t>::max() / 2)
      count = std::max(count, values.capacity() * 2);
    // Old storage stays charged during growth and conservatively thereafter.
    Admit(count, sizeof(T)); values.reserve(count); Admit(values.capacity() - count, sizeof(T));
  }
  void Text(std::vector<std::u16string>& arena, std::u16string_view text) {
    Step(text.size()); Admit(text.size(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
    auto& owned = arena.emplace_back(text);
    Admit(owned.capacity() - text.size(), sizeof(char16_t));
  }
  ObjectConstantStorage Run(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, abc::EntityOffset method, const analysis::ValueProgram& graph) {
    std::vector<BufferUse> uses;
    std::size_t count = 0;
    for (const auto& op : graph.ops) {
      Step(); const auto* object = std::get_if<analysis::ValueObjectAllocation>(&op.operation);
      const auto* array = std::get_if<analysis::ValueArrayAllocation>(&op.operation);
      count += op.life == analysis::ValueLife::kLive && ((object && object->literal) || (array && array->literal));
    }
    Grow(uses, count);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      Step(); const auto& op = graph.ops[i];
      const auto* object = std::get_if<analysis::ValueObjectAllocation>(&op.operation);
      if (op.life == analysis::ValueLife::kLive && object && object->literal)
        uses.push_back({*object->literal, {static_cast<std::uint32_t>(i)}, AggregateKind::kObject});
      const auto* array = std::get_if<analysis::ValueArrayAllocation>(&op.operation);
      if (op.life == analysis::ValueLife::kLive && array && array->literal)
        uses.push_back({*array->literal, {static_cast<std::uint32_t>(i)}, AggregateKind::kArray});
    }
    std::sort(uses.begin(), uses.end(), [&](const auto& a, const auto& b) { Step(); return a.kind == b.kind ? a.slot < b.slot : a.kind < b.kind; });
    for (std::size_t first = 0; first < uses.size();) {
      Step(); auto end = first + 1;
      while (end < uses.size() && uses[end].slot == uses[first].slot && uses[end].kind == uses[first].kind) { Step(); ++end; }
      const MethodConstants context(file, header, index, method, {left_.bytes});
      const auto origin = graph.ops[uses[first].operation.value].origin;
      if (origin.value >= graph.origins.size()) throw Error::kObjectLiteral;
      const auto kind = uses[first].kind;
      ObjectBuffer object_buffer;
      abc::LiteralArray array_buffer;
      if (kind == AggregateKind::kObject) {
        auto read = ReadObjectBuffer({uses[first].slot}, graph.origins[origin.value].pc, context, {left_.bytes, left_.work}, true);
        const auto* buffer = std::get_if<ObjectBuffer>(&read);
        if (!buffer) {
          const auto& failure = std::get<BodyEmitFailure>(read);
          if (failure.metadata_error) throw *failure.metadata_error;
          if (failure.object_error == ObjectBufferError::kWorkLimit) throw Error::kWorkLimit;
          throw failure.kind == BodyEmitError::kResourceLimit ? Error::kResourceLimit : Error::kObjectLiteral;
        }
        Admit(buffer->allocated_bytes, 1); Step(buffer->work);
        object_buffer = std::get<ObjectBuffer>(std::move(read));
      } else {
        auto reference = abc::ResolvePoolReference(file, header, index, method,
            uses[first].slot, abc::PoolEntityKind::kLiteralArray);
        if (const auto* error = std::get_if<abc::MetadataError>(&reference)) throw *error;
        auto read = abc::ReadLiteralArray(file, header, std::get<abc::PoolReference>(reference).offset, {left_.bytes});
        if (const auto* error = std::get_if<abc::LiteralFailure>(&read)) {
          if (error->kind == abc::LiteralError::kFileChanged) throw abc::MetadataError::kFileChanged;
          if (error->kind == abc::LiteralError::kRead) throw abc::MetadataError::kRead;
          throw error->kind == abc::LiteralError::kResourceLimit ? Error::kResourceLimit : Error::kObjectLiteral;
        }
        array_buffer = std::get<abc::LiteralArray>(std::move(read));
        Admit(array_buffer.entries.capacity(), sizeof(abc::LiteralEntry));
      }
      const auto fields = kind == AggregateKind::kObject ? object_buffer.keys.size() : array_buffer.entries.size();
      constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
      if (fields > kMaxId - out_.properties.size() || fields > kMaxId - out_.names.size() ||
          fields > kMaxId - out_.texts.size()) throw Error::kResourceLimit;
      Grow(out_.properties, out_.properties.size() + fields);
      Grow(out_.names, out_.names.size() + fields);
      Grow(out_.texts, out_.texts.size() + fields);
      const AggregateConstant object{static_cast<std::uint32_t>(out_.properties.size()), static_cast<std::uint32_t>(fields), kind};
      for (std::size_t i = 0; i < fields; ++i) {
        Step(); const NameId key{static_cast<std::uint32_t>(out_.names.size())};
        if (kind == AggregateKind::kObject) Text(out_.names, object_buffer.keys[i]);
        else {
          // Native array syntax uses dense ordered keys. No holes, methods or
          // embedded objects are silently converted to primitive elements.
          constexpr auto kIndexDigits = std::numeric_limits<std::uint32_t>::digits10 + 1;
          char digits[kIndexDigits];
          const auto converted = std::to_chars(digits, digits + kIndexDigits, static_cast<std::uint32_t>(i));
          if (converted.ec != std::errc{}) throw Error::kResourceLimit;
          char16_t key_text[kIndexDigits];
          const auto length = static_cast<std::size_t>(converted.ptr - digits);
          for (std::size_t digit = 0; digit < length; ++digit) key_text[digit] = digits[digit];
          Text(out_.names, {key_text, length});
        }
        const auto& entry = kind == AggregateKind::kObject ? object_buffer.literal.entries[object_buffer.values[i]] : array_buffer.entries[i];
        TreeLiteral value;
        if (kind == AggregateKind::kObject && IsBufferMethod(object_buffer, i)) {
          out_.properties.push_back({key, NullLiteral{}, true});
          continue;
        }
        if (entry.tag == abc::LiteralTag::kString) {
          auto decoded = abc::ReadStringEntity(file, header, std::get<abc::EntityOffset>(entry.value), {left_.bytes});
          const auto* text = std::get_if<std::u16string>(&decoded);
          if (!text) throw std::get<abc::MetadataError>(decoded);
          Admit(text->capacity(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
          value = TextId{static_cast<std::uint32_t>(out_.texts.size())}; Text(out_.texts, *text);
        } else {
          const auto* raw = std::get_if<abc::LiteralBits>(&entry.value);
          if (!raw) throw Error::kObjectLiteral;
          const auto bits = raw->value;
          switch (entry.tag) {
            case abc::LiteralTag::kNull: value = NullLiteral{}; break;
            case abc::LiteralTag::kBoolean:
              if (bits > 1) throw Error::kObjectLiteral;
              value = BooleanLiteral{bits != 0}; break;
            case abc::LiteralTag::kDouble: value = NumberLiteral{bits}; break;
            case abc::LiteralTag::kInteger:
              // v7 ExtractObjectDatas/GetDatasIgnoreType use JSTaggedValue(uint32_t), not a
              // signed reinterpretation. Large u32 values remain exact doubles.
              if (bits > UINT32_MAX) throw Error::kObjectLiteral;
              value = bits <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
                  ? TreeLiteral{IntegerLiteral{static_cast<std::int32_t>(bits)}}
                  : TreeLiteral{NumberLiteral{std::bit_cast<std::uint64_t>(static_cast<double>(bits))}};
              break;
            default: throw Error::kObjectLiteral;
          }
        }
        out_.properties.push_back({key, value});
      }
      for (auto i = first; i < end; ++i) { Step(); out_.operations[uses[i].operation.value] = object; }
      first = end;
    }
    return {initial_.bytes - left_.bytes, initial_.work - left_.work};
  }
 private:
  TreeConstants& out_;
  analysis::SemanticBudget initial_, left_;
};
}  // namespace
std::variant<ObjectConstantStorage, TreeConstantError, abc::MetadataError> ResolveObjectConstants(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    abc::EntityOffset method, const analysis::ValueProgram& graph, TreeConstants& constants,
    analysis::SemanticBudget budget) {
  try { return Resolver(constants, budget).Run(file, header, index, method, graph);
  } catch (Error error) { return error;
  } catch (abc::MetadataError error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
