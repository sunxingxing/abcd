#include "abcd/source/semantic_source.h"

#include <charconv>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "abcd/abc/pool_reference.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/string_literal.h"
#include "element_name.h"

namespace abcd::source {
namespace {
using analysis::Accumulator;
using analysis::ScalarExpression;
using analysis::ScalarValue;

class SourcePrinter {
 public:
  SourcePrinter(const SourceAst& ast, const analysis::SemanticMethod& method,
                const abc::FrameLayout& frame, SourceForm form,
                std::string_view name, const MethodConstants& constants,
                std::size_t bytes)
      : ast_(ast), method_(method), frame_(frame), form_(form), name_(name),
        constants_(constants), limit_(bytes) {}

  std::variant<PrintedSource, SourceFailure> Print() {
    try {
      if (!IsSourceIdentifier(name_))
        return SourceFailure{SourceError::kOutput, {}};
      if (frame_.registers > limit_ / sizeof(std::string))
        return SourceFailure{SourceError::kResourceLimit, {}};
      bindings_.reserve(frame_.registers);
      views_.reserve(frame_.registers);
      for (std::uint32_t i = 0; i < frame_.registers; ++i)
        bindings_.push_back("$r" + std::to_string(i));
      for (const auto& binding : bindings_)
        views_.push_back(binding);
      names_ = {"$acc", views_};
      if (ast_.statements.size() > limit_ / sizeof(GeneratedSpan))
        return SourceFailure{SourceError::kResourceLimit, {}};
      result_.spans.reserve(ast_.statements.size());
      if (form_ == SourceForm::kClassConstructor) {
        Text("export default class ");
        Text(name_);
        Text(" {\nconstructor(");
      } else {
        Text("export default function ");
        Text(name_);
        Text("(");
      }
      for (std::uint32_t i = 0; i < frame_.parameters; ++i) {
        if (i) Text(", ");
        Text("$p");
        Number(i);
      }
      Text(") {\n\"use strict\";\nlet $acc;\n");
      for (std::uint32_t i = 0; i < frame_.registers; ++i) {
        Text("let ");
        Text(bindings_[i]);
        if (frame_.function == abc::RegisterId{i}) {
          Text(" = ");
          Text(name_);
        } else if (frame_.new_target == abc::RegisterId{i}) {
          Text(" = new.target");
        } else if (frame_.this_value == abc::RegisterId{i}) {
          Text(" = this");
        } else if (i >= frame_.first_parameter.value &&
                   i - frame_.first_parameter.value < frame_.parameters) {
          Text(" = $p");
          Number(i - frame_.first_parameter.value);
        }
        Text(";\n");
      }
      Region(ast_.regions.root);
      Text(form_ == SourceForm::kClassConstructor ? "}\n}\n" : "}\n");
      return std::move(result_);
    } catch (const SourceFailure& failure) {
      return failure;
    } catch (const std::bad_alloc&) {
      return SourceFailure{SourceError::kResourceLimit, {}};
    } catch (const std::length_error&) {
      return SourceFailure{SourceError::kResourceLimit, {}};
    }
  }

 private:
  void Text(std::string_view value) {
    if (value.size() > limit_ - result_.text.size())
      throw SourceFailure{SourceError::kOutput, current_pc_};
    result_.text.append(value);
    position_.byte += value.size();
    for (const char ch : value) {
      if (ch == '\n') {
        ++position_.line;
        position_.column = 0;
      } else {
        ++position_.column;
      }
    }
  }
  void Number(std::uint32_t value) {
    char buffer[std::numeric_limits<std::uint32_t>::digits10 + 1];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec != std::errc{})
      throw SourceFailure{SourceError::kOutput, current_pc_};
    Text({buffer, static_cast<std::size_t>(converted.ptr - buffer)});
  }
  void Scalar(const ScalarExpression& expression) {
    std::ostringstream stream;
    const auto emitted = WriteScalarExpression(
        expression, names_, stream, {limit_ - result_.text.size()});
    if (!std::holds_alternative<std::size_t>(emitted))
      throw SourceFailure{SourceError::kOutput, current_pc_};
    Text(stream.str());
  }
  void Value(const ScalarValue& value) { Scalar(ScalarExpression{value}); }
  void Slot(const analysis::ScalarSlot& slot) {
    std::visit([&](const auto& value) { Value(ScalarValue{value}); }, slot);
  }
  void PoolName(abc::PoolIndex slot) {
    const auto reference = abc::ResolvePoolReference(
        constants_.file, constants_.header, constants_.index,
        constants_.method, slot, abc::PoolEntityKind::kString);
    const auto* pool = std::get_if<abc::PoolReference>(&reference);
    if (!pool) throw SourceFailure{SourceError::kConstant, current_pc_};
    const auto string = abc::ReadStringEntity(
        constants_.file, constants_.header, pool->offset,
        constants_.string_budget);
    const auto* name = std::get_if<std::u16string>(&string);
    if (!name || IsElementName(*name))
      throw SourceFailure{SourceError::kConstant, current_pc_};
    std::ostringstream stream;
    const auto written = WriteStringLiteral(
        *name, stream, {limit_ - result_.text.size()});
    if (!std::holds_alternative<std::size_t>(written))
      throw SourceFailure{SourceError::kOutput, current_pc_};
    Text(stream.str());
  }
  void Expression(const SourceExpression& expression) {
    if (const auto* scalar = std::get_if<ScalarExpression>(&expression)) {
      Scalar(*scalar);
    } else {
      const auto& member = std::get<MemberExpression>(expression);
      Text("(");
      Value(member.object);
      Text(")[");
      PoolName(member.name);
      Text("]");
    }
  }
  void Label(analysis::BlockId id) {
    Text("next_");
    Number(id.value);
  }
  void Statement(const SourceStatement& statement) {
    current_pc_ = statement.pc;
    const auto begin = position_;
    std::visit(
        [&](const auto& operation) {
          using T = std::decay_t<decltype(operation)>;
          if constexpr (std::is_same_v<T, SourceAssignment>) {
            Slot(operation.target);
            Text(" = ");
            Expression(operation.value);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceMemberStore>) {
            Text("(");
            Value(operation.object);
            Text(")[");
            PoolName(operation.name);
            Text("] = ");
            Value(operation.value);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceConditionalBreak>) {
            Text("if (");
            Scalar(ScalarExpression{operation.condition});
            Text(") break ");
            Label(operation.target);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceBreak>) {
            Text("break ");
            Label(operation.target);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceReturn>) {
            Text("return ");
            Value(operation.value);
            Text(";\n");
          } else {
            static_assert(std::is_same_v<T, analysis::NoOperation>);
          }
        },
        statement.operation);
    result_.spans.push_back({statement.pc, begin, position_});
  }
  void Region(const analysis::RegionNode& node) {
    std::visit(
        [&](const auto& region) {
          using T = std::decay_t<decltype(region)>;
          if constexpr (std::is_same_v<T, analysis::RegionBlock>) {
            if (region.id.value >= method_.flow.blocks.size())
              throw SourceFailure{SourceError::kOutput, current_pc_};
            const auto& block = method_.flow.blocks[region.id.value];
            const auto end = std::uint64_t(block.first_instruction) +
                             block.instruction_count;
            if (end > ast_.statements.size())
              throw SourceFailure{SourceError::kOutput, current_pc_};
            for (std::size_t i = block.first_instruction; i < end; ++i)
              Statement(ast_.statements[i]);
            if (region.id.value >= ast_.fallthrough.size())
              throw SourceFailure{SourceError::kOutput, current_pc_};
            if (ast_.fallthrough[region.id.value]) {
              Text("break ");
              Label(*ast_.fallthrough[region.id.value]);
              Text(";\n");
              if (result_.spans.empty())
                throw SourceFailure{SourceError::kOutput, current_pc_};
              result_.spans.back().end = position_;
            }
          } else if constexpr (std::is_same_v<T, analysis::RegionLabel>) {
            Label(region.exit);
            Text(": {\n");
            Region(*region.body);
            Text("}\n");
          } else if constexpr (std::is_same_v<T, analysis::RegionSequence>) {
            for (const auto& child : region.children) Region(child);
          }
        },
        node.value);
  }

  const SourceAst& ast_;
  const analysis::SemanticMethod& method_;
  const abc::FrameLayout& frame_;
  SourceForm form_;
  std::string_view name_;
  const MethodConstants& constants_;
  std::size_t limit_;
  std::vector<std::string> bindings_;
  std::vector<std::string_view> views_;
  ScalarNames names_{};
  PrintedSource result_;
  GeneratedPosition position_;
  abc::CodeOffset current_pc_{};
};
}  // namespace

std::variant<PrintedSource, SourceFailure> PrintSourceAst(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, std::size_t output_bytes) {
  return SourcePrinter(ast, method, frame, form, name, constants,
                       output_bytes).Print();
}
}  // namespace abcd::source
