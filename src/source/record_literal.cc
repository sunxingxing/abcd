#include "record_literal.h"

#include <algorithm>
#include <array>
#include <ostream>
#include <type_traits>

#include "abcd/source/scalar_expression.h"
#include "abcd/source/string_literal.h"

namespace abcd::source {
namespace {
// A hostile ABC can create a very deep tree even when its byte size is small.
constexpr std::size_t kMaxLiteralDepth = 256;
constexpr std::size_t kIndentWidth = 2;
constexpr std::size_t kIndentChunkBytes = 64;
constexpr auto kIndentChunk = [] {
  std::array<char, kIndentChunkBytes> spaces{};
  spaces.fill(' ');
  return spaces;
}();

class LiteralWriter {
 public:
  LiteralWriter(std::span<const RecordLiteralNode> nodes,
                std::ostream& output, EmitBudget budget)
      : nodes_(nodes), output_(output), remaining_(budget.bytes) {}

  RecordLiteralResult Write(std::size_t root) {
    try {
      Node(root, 0);
      return written_;
    } catch (EmitError error) {
      return error;
    } catch (RecordLiteralError error) {
      return error;
    } catch (const std::ios_base::failure&) {
      return EmitError::kWrite;
    }
  }

 private:
  void Text(std::string_view value) {
    if (value.size() > remaining_) throw EmitError::kOutputLimit;
    output_.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output_) throw EmitError::kWrite;
    remaining_ -= value.size();
    written_ += value.size();
  }
  void Indent(std::size_t depth) {
    std::size_t bytes = depth * kIndentWidth;
    while (bytes) {
      const auto chunk = std::min(bytes, kIndentChunk.size());
      Text({kIndentChunk.data(), chunk});
      bytes -= chunk;
    }
  }
  void Account(const std::variant<std::size_t, EmitError>& result) {
    if (const auto* error = std::get_if<EmitError>(&result)) throw *error;
    const auto bytes = std::get<std::size_t>(result);
    remaining_ -= bytes;
    written_ += bytes;
  }
  void Atom(const StaticAtom& atom) {
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, StaticUndefined>)
            Text("void 0");
          else if constexpr (std::is_same_v<T, StaticNull>)
            Text("null");
          else if constexpr (std::is_same_v<T, bool>)
            Text(value ? "true" : "false");
          else if constexpr (std::is_same_v<T, std::u16string>)
            Account(WriteStringLiteral(value, output_, {remaining_}));
          else {
            const analysis::ScalarValue scalar = [&]() -> analysis::ScalarValue {
              if constexpr (std::is_same_v<T, std::int32_t>)
                return analysis::IntegerLiteral{value};
              else
                return analysis::Float64Bits{value.bits};
            }();
            Account(WriteScalarExpression(scalar, {"", {}}, output_,
                                          {remaining_}));
          }
        },
        atom);
  }
  void Value(const RecordLiteralValue& value, std::size_t parent,
             std::size_t depth) {
    if (const auto* atom = std::get_if<StaticAtom>(&value)) {
      Atom(*atom);
      return;
    }
    const auto child = std::get<RecordNodeRef>(value).id;
    if (child <= parent) throw RecordLiteralError::kInvalidGraph;
    Node(child, depth + 1);
  }
  void Node(std::size_t id, std::size_t depth) {
    if (id >= nodes_.size()) throw RecordLiteralError::kInvalidGraph;
    if (depth >= kMaxLiteralDepth) throw RecordLiteralError::kDepthLimit;
    const auto& node = nodes_[id];
    switch (node.kind) {
      case RecordLiteralKind::kObject:
        if (!node.elements.empty()) throw RecordLiteralError::kInvalidGraph;
        Text("({");
        for (std::size_t i = 0; i < node.fields.size(); ++i) {
          if (i) Text(",");
          Text("\n");
          Indent(depth + 1);
          Text("[");
          Account(WriteStringLiteral(node.fields[i].key, output_,
                                     {remaining_}));
          Text("]: ");
          Value(node.fields[i].value, id, depth);
        }
        if (!node.fields.empty()) {
          Text("\n");
          Indent(depth);
        }
        Text("})");
        break;
      case RecordLiteralKind::kArray:
        if (!node.fields.empty()) throw RecordLiteralError::kInvalidGraph;
        Text("[");
        for (std::size_t i = 0; i < node.elements.size(); ++i) {
          if (i) Text(",");
          Text("\n");
          Indent(depth + 1);
          Value(node.elements[i], id, depth);
        }
        if (!node.elements.empty()) {
          Text("\n");
          Indent(depth);
        }
        Text("]");
        break;
    }
  }
  std::span<const RecordLiteralNode> nodes_;
  std::ostream& output_;
  std::size_t remaining_;
  std::size_t written_ = 0;
};
}  // namespace

RecordLiteralResult WriteRecordLiteral(
    std::span<const RecordLiteralNode> nodes, std::size_t root,
    std::ostream& output, EmitBudget budget) {
  return LiteralWriter(nodes, output, budget).Write(root);
}
}  // namespace abcd::source
