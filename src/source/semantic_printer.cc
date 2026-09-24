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
#include "leaf_creation.h"
#include "lexical_name.h"
#include "capture_stream.h"
#include "semantic_capture.h"
#include "object_buffer.h"
#include "record_array.h"
#include "record_literal.h"
#include "semantic_fields.h"
#include "creation_expansion.h"

namespace abcd::source {
namespace {
using analysis::Accumulator;
using analysis::ScalarExpression;
using analysis::ScalarValue;

class SourcePrinter {
 public:
  enum class CaptureMode { kReject, kTemplate };
  SourcePrinter(const SourceAst& ast, const analysis::SemanticMethod& method,
                const abc::FrameLayout& frame, SourceForm form,
                std::string_view name, const MethodConstants& constants,
                std::size_t bytes, CaptureMode captures = CaptureMode::kReject,
                const analysis::SemanticProof* proof = nullptr)
      : ast_(ast), method_(method), frame_(frame), form_(form), name_(name),
        constants_(constants), limit_(bytes), capture_mode_(captures), proof_(proof),
        expansion_(constants.method, bytes, kDefaultMethodWork) {}

  std::variant<PrintedTemplate, SourceFailure> PrintTemplate(ScalarNames names) {
    auto printed = PrintBody(names);
    if (const auto* failure = std::get_if<SourceFailure>(&printed)) return *failure;
    auto& source = std::get<PrintedSource>(printed);
    return PrintedTemplate{std::move(source.text), std::move(source.spans),
                            std::move(captures_)};
  }

  std::variant<PrintedSource, SourceFailure> PrintBody(ScalarNames names) {
    try {
      if (const auto failure = VerifyOwnFields(ast_, constants_)) return *failure;
      names_ = names;
      DeclareLocals();
      if (ast_.statements.size() > limit_ / sizeof(GeneratedSpan))
        return SourceFailure{SourceError::kResourceLimit, {}};
      result_.spans.reserve(ast_.statements.size());
      if (capture_mode_ == CaptureMode::kTemplate) {
        const auto span_bytes = result_.spans.capacity() * sizeof(GeneratedSpan);
        if (span_bytes > limit_ ||
            ast_.statements.size() >
                (limit_ - span_bytes) / sizeof(SourceCapture))
          return SourceFailure{SourceError::kResourceLimit, {}};
        captures_.reserve(ast_.statements.size());
        if (captures_.capacity() >
            (limit_ - span_bytes) / sizeof(SourceCapture))
          return SourceFailure{SourceError::kResourceLimit, {}};
      }
      Region(ast_.regions.root);
      return std::move(result_);
    } catch (const SourceFailure& failure) {
      return failure;
    } catch (const std::bad_alloc&) {
      return SourceFailure{SourceError::kResourceLimit, {}};
    } catch (const std::length_error&) {
      return SourceFailure{SourceError::kResourceLimit, {}};
    }
  }

  std::variant<PrintedSource, SourceFailure> Print() {
    try {
      if (const auto failure = VerifyOwnFields(ast_, constants_)) return *failure;
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
      DeclareLocals();
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
  class LeafBuffer : public CaptureBuffer {
   public:
    LeafBuffer(SourcePrinter& printer, const FunctionExpression& function)
        : printer_(printer), function_(function) {}
    bool Capture(analysis::CapturedSlot capture) override {
      if (!printer_.proof_)
        throw SourceFailure{SourceError::kCreation, printer_.current_pc_};
      const auto resolved = ResolveCreationCapture(
          printer_.method_, *printer_.proof_, function_, capture);
      if (const auto* failure = std::get_if<SourceFailure>(&resolved)) throw *failure;
      if (const auto* outer = std::get_if<analysis::CapturedSlot>(&resolved)) {
        printer_.Capture(*outer);
        return true;
      }
      printer_.LocalName(std::get<analysis::LocalCell>(resolved));
      return true;
    }
   private:
    std::streamsize xsputn(const char* text, std::streamsize count) override {
      if (count < 0) return 0;
      printer_.Text({text, static_cast<std::size_t>(count)});
      return count;
    }
    int_type overflow(int_type ch) override {
      if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
      const char value = traits_type::to_char_type(ch);
      printer_.Text({&value, 1});
      return ch;
    }
    SourcePrinter& printer_;
    const FunctionExpression& function_;
  };
  void Capture(analysis::CapturedSlot cell) {
    if (capture_mode_ != CaptureMode::kTemplate)
      throw SourceFailure{SourceError::kUnsupportedOperation, current_pc_};
    // The insertion remains unresolved through every wrapper. Its cursor is
    // part of the parent creation span, even if no surrounding byte is emitted.
    if (captures_.size() >= captures_.capacity())
      throw SourceFailure{SourceError::kResourceLimit, current_pc_};
    captures_.push_back({position_, cell});
    ++position_.captures;
  }
  void LocalName(analysis::LocalCell cell) {
    const auto name = capture_mode_ == CaptureMode::kTemplate
                          ? LexicalName{cell, constants_.method}
                          : LexicalName{cell};
    Text(name.Text());
  }
  void Local(analysis::LocalCellId id) {
    if (id.value >= ast_.local_cells.size())
      throw SourceFailure{SourceError::kInvalidEffect, current_pc_};
    LocalName(ast_.local_cells[id.value]);
  }
  void DeclareLocals() {
    if (ast_.local_cells.empty()) return;
    const auto reserved = [](std::string_view name) {
      return name.starts_with("$lex") || name.starts_with("$inner");
    };
    if (reserved(names_.accumulator) || reserved(names_.current_function) ||
        reserved(name_)) throw SourceFailure{SourceError::kOutput, {}};
    for (const auto name : names_.registers)
      if (reserved(name)) throw SourceFailure{SourceError::kOutput, {}};
    for (const auto name : constants_.source_parameters)
      if (reserved(name)) throw SourceFailure{SourceError::kOutput, {}};
    if (constants_.global_scope &&
        (reserved(constants_.global_scope->self_binding) ||
         reserved(constants_.global_scope->class_binding)))
      throw SourceFailure{SourceError::kOutput, {}};
    if (constants_.global_scope)
      for (const auto name : constants_.global_scope->parameter_bindings)
        if (reserved(name)) throw SourceFailure{SourceError::kOutput, {}};
    for (std::size_t i = 0; i < ast_.local_cells.size(); ++i) {
      Text("let ");
      Local(analysis::LocalCellId{static_cast<std::uint32_t>(i)});
      Text(";\n");
    }
  }
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
  enum class StringUse { kValue, kPropertyName };
  std::u16string PoolText(abc::PoolIndex slot) {
    const auto reference = abc::ResolvePoolReference(
        constants_.file, constants_.header, constants_.index,
        constants_.method, slot, abc::PoolEntityKind::kString);
    const auto* pool = std::get_if<abc::PoolReference>(&reference);
    if (!pool) throw SourceFailure{SourceError::kConstant, current_pc_};
    auto string = abc::ReadStringEntity(
        constants_.file, constants_.header, pool->offset,
        constants_.string_budget);
    auto* name = std::get_if<std::u16string>(&string);
    if (!name) throw SourceFailure{SourceError::kConstant, current_pc_};
    return std::move(*name);
  }
  void PoolString(abc::PoolIndex slot, StringUse use) {
    const auto name = PoolText(slot);
    if (use == StringUse::kPropertyName && IsElementName(name))
      throw SourceFailure{SourceError::kConstant, current_pc_};
    std::ostringstream stream;
    const auto written = WriteStringLiteral(
        name, stream, {limit_ - result_.text.size()});
    if (!std::holds_alternative<std::size_t>(written))
      throw SourceFailure{SourceError::kOutput, current_pc_};
    Text(stream.str());
  }
  void PoolName(abc::PoolIndex slot) {
    PoolString(slot, StringUse::kPropertyName);
  }
  template <typename Call>
  void Arguments(const Call& call) {
    Text("(");
    for (std::uint32_t i = 0; i < call.Count(); ++i) {
      if (i) Text(", ");
      const auto argument = call.At(i);
      if (!argument) throw SourceFailure{SourceError::kOutput, current_pc_};
      Value(ScalarValue{*argument});
    }
    Text(")");
  }
  void Expression(const SourceExpression& expression) {
    if (const auto* scalar = std::get_if<ScalarExpression>(&expression)) {
      Scalar(*scalar);
    } else if (const auto* rest = std::get_if<analysis::RestCopy>(&expression)) {
      if (!names_.rest || names_.rest->first_argument != rest->first_argument)
        throw SourceFailure{SourceError::kInvalidEffect, current_pc_};
      // The owner proves a single prologue copy and emits the matching native
      // rest formal. Do not slice arguments or reuse it for a second allocation.
      std::ostringstream stream;
      const auto emitted = WriteScalarExpression(
          ScalarValue{Accumulator{}}, {names_.rest->name, {}}, stream,
          {limit_ - result_.text.size()});
      if (!std::holds_alternative<std::size_t>(emitted))
        throw SourceFailure{SourceError::kOutput, current_pc_};
      Text(stream.str());
    } else if (const auto* local = std::get_if<LocalExpression>(&expression)) {
      Local(local->cell);
    } else if (const auto* created =
                   std::get_if<FunctionExpression>(&expression)) {
      LeafBuffer buffer(*this, *created);
      std::ostream stream(&buffer);
      stream.exceptions(std::ios::badbit | std::ios::failbit);
      const auto remaining = limit_ - result_.text.size();
      const auto emitted = WriteLeafFunction(
          created->creation, current_pc_, constants_, stream,
          {remaining, analysis::kDefaultSemanticWork, {remaining}},
          analysis::CapturedValues::kLanguage,
          constants_.expansion ? constants_.expansion : &expansion_);
      if (const auto* failure = std::get_if<BodyEmitFailure>(&emitted)) {
        const bool resource = failure->kind == BodyEmitError::kResourceLimit ||
                              failure->kind == BodyEmitError::kWorkLimit ||
                              failure->emit_error == EmitError::kOutputLimit;
        throw SourceFailure{resource ? SourceError::kResourceLimit
                                     : SourceError::kCreation, current_pc_};
      }
    } else if (const auto* captured = std::get_if<CapturedExpression>(&expression)) {
      // A zero-width typed insertion, never a string placeholder. Parent
      // binding is mandatory even when this load's register result is unused.
      Capture(captured->cell);
    } else if (const auto* string = std::get_if<StringExpression>(&expression)) {
      PoolString(string->value, StringUse::kValue);
    } else if (const auto* call = std::get_if<CallExpression>(&expression)) {
      Text("(0, ");
      Value(ScalarValue{Accumulator{}});
      Text(")");
      Arguments(call->call);
    } else if (const auto* call = std::get_if<MemberCallExpression>(&expression)) {
      Text("(");
      Value(ScalarValue{call->call.receiver});
      Text(")[");
      PoolName(call->name);
      Text("]");
      Arguments(call->call);
    } else if (const auto* call = std::get_if<analysis::ConstructorCall>(&expression)) {
      Text("new (");
      Value(ScalarValue{call->inputs.first});
      Text(")(");
      for (std::uint32_t i = 0; i < call->ArgumentCount(); ++i) {
        if (i) Text(", ");
        const auto argument = call->ArgumentAt(i);
        if (!argument) throw SourceFailure{SourceError::kOutput, current_pc_};
        Value(ScalarValue{*argument});
      }
      Text(")");
    } else if (const auto* global = std::get_if<GlobalExpression>(&expression)) {
      const auto name = PoolText(global->name);
      if (!constants_.global_scope ||
          !AllowsGlobalReference(name, *constants_.global_scope))
        throw SourceFailure{SourceError::kConstant, current_pc_};
      for (const auto ch : name) {
        const char ascii = static_cast<char>(ch);
        Text({&ascii, 1});
      }
    } else if (std::holds_alternative<analysis::EmptyObjectCreation>(expression)) {
      Text("({})");
    } else if (const auto* buffered =
                   std::get_if<analysis::BufferedObjectCreation>(&expression)) {
      std::ostringstream stream;
      const auto emitted = WriteObjectBuffer(
          *buffered, current_pc_, constants_, stream,
          {limit_ - result_.text.size()}, {});
      if (!std::holds_alternative<std::size_t>(emitted))
        throw SourceFailure{SourceError::kConstant, current_pc_};
      Text(stream.str());
    } else if (const auto* array =
                   std::get_if<analysis::BufferedArrayCreation>(&expression)) {
      const auto remaining = limit_ - result_.text.size();
      const auto decoded = ReadBufferedArray(
          constants_.file, constants_.header, constants_.index,
          constants_.method, *array, remaining, constants_.string_budget);
      const auto* buffer = std::get_if<BufferedArray>(&decoded);
      if (!buffer) {
        const auto* error = std::get_if<BufferedArrayError>(&decoded);
        const auto* metadata = std::get_if<abc::MetadataError>(&decoded);
        const auto* literal = std::get_if<abc::LiteralFailure>(&decoded);
        const bool resource =
            (error && *error == BufferedArrayError::kResourceLimit) ||
            (metadata && *metadata == abc::MetadataError::kResourceLimit) ||
            (literal && literal->kind == abc::LiteralError::kResourceLimit);
        throw SourceFailure{resource ? SourceError::kResourceLimit
                                     : SourceError::kConstant, current_pc_};
      }
      // Each execution allocates a fresh array. Never hoist the buffer into a
      // shared object or turn unsupported holes into explicit undefined values.
      std::ostringstream stream;
      const auto emitted = WriteDenseArray(
          buffer->elements, stream, {remaining - buffer->retained_bytes});
      if (const auto* error = std::get_if<EmitError>(&emitted))
        throw SourceFailure{*error == EmitError::kOutputLimit
                                ? SourceError::kResourceLimit : SourceError::kOutput,
                            current_pc_};
      Text(stream.str());
    } else if (const auto* module = std::get_if<ModuleExpression>(&expression)) {
      if (!constants_.module)
        throw SourceFailure{SourceError::kConstant, current_pc_};
      const auto slot = module->binding.slot.value;
      if (module->binding.space == analysis::ModuleSpace::kExternal) {
        const auto& owner = *constants_.module;
        if (slot >= owner.imports.size() + owner.namespace_imports.size())
          throw SourceFailure{SourceError::kConstant, current_pc_};
        Text("$import");
      } else {
        if (!constants_.global_scope ||
            slot >= constants_.global_scope->module_slots ||
            slot >= constants_.module->local_exports.size())
          throw SourceFailure{SourceError::kConstant, current_pc_};
        Text("$local");
      }
      Number(slot);
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
          } else if constexpr (std::is_same_v<T, SourceLocalStore>) {
            Local(operation.cell);
            Text(" = ");
            Slot(operation.value);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceMemberStore>) {
            Text("(");
            Value(operation.object);
            Text(")[");
            PoolName(operation.name);
            Text("] = ");
            Value(operation.value);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceOwnStore>) {
            // Pinned RuntimeDefineField sets all data attributes true. On a
            // private ordinary object with this existing data key, strict Set
            // has the same effect and cannot consult an inherited setter.
            Text("(");
            Value(ScalarValue{operation.write.object});
            Text(")[");
            PoolName(operation.write.name);
            Text("] = ");
            Value(ScalarValue{Accumulator{}});
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
          } else if constexpr (std::is_same_v<T, SourceStagedCall>) {
            constexpr std::size_t kMaxCallDepth = 128;
            if (call_depth_ >= kMaxCallDepth || !operation.call.Count() ||
                operation.call_statement.value >= ast_.statements.size())
              throw SourceFailure{SourceError::kResourceLimit, current_pc_};
            ++call_depth_;
            // Keep the property Reference alive across argument evaluation;
            // registers are private, and the saved callee has no escaping use.
            Slot(analysis::ScalarSlot{Accumulator{}});
            Text(" = (");
            Value(ScalarValue{Accumulator{}});
            Text(")[");
            PoolName(operation.name);
            Text("]((() => {\n");
            Statements(operation.first_argument_statement.value,
                       operation.call_statement.value);
            current_pc_ = ast_.statements[operation.call_statement.value].pc;
            Text("return ");
            const auto first = operation.call.At(0);
            if (!first) throw SourceFailure{SourceError::kOutput, current_pc_};
            Value(ScalarValue{*first});
            Text(";\n})()");
            for (std::uint32_t i = 1; i < operation.call.Count(); ++i) {
              Text(", ");
              const auto argument = operation.call.At(i);
              if (!argument) throw SourceFailure{SourceError::kOutput, current_pc_};
              Value(ScalarValue{*argument});
            }
            Text(");\n");
            --call_depth_;
          } else {
            static_assert(std::is_same_v<T, analysis::NoOperation>);
          }
        },
        statement.operation);
    if (statement.fused_read) {
      const auto relocate = [&](abc::CodeOffset pc, std::size_t distance) {
        if (!distance || result_.spans.size() < distance ||
            result_.spans[result_.spans.size() - distance].pc != pc)
          throw SourceFailure{SourceError::kOutput, current_pc_};
        auto& span = result_.spans[result_.spans.size() - distance];
        span.begin = begin;
        span.end = position_;
      };
      if (statement.fused_spill && statement.fused_reload) {
        relocate(*statement.fused_read, statement.read_distance);
        relocate(*statement.fused_spill, statement.spill_distance);
        relocate(*statement.fused_reload, statement.reload_distance);
      } else {
        relocate(*statement.fused_read, statement.read_distance);
      }
    }
    if (statement.previous_origin) {
      if (result_.spans.empty() ||
          result_.spans.back().pc != *statement.previous_origin ||
          begin.byte != position_.byte)
        throw SourceFailure{SourceError::kOutput, current_pc_};
      const auto previous = result_.spans.back();
      result_.spans.push_back({statement.pc, previous.begin, previous.end});
    } else {
      result_.spans.push_back({statement.pc, begin, position_});
    }
    if (const auto* staged = std::get_if<SourceStagedCall>(&statement.operation))
      result_.spans.push_back({ast_.statements[staged->call_statement.value].pc,
                               begin, position_});
  }
  void Statements(std::size_t first, std::size_t end) {
    if (first > end || end > ast_.statements.size())
      throw SourceFailure{SourceError::kOutput, current_pc_};
    for (std::size_t i = first; i < end;) {
      const auto& statement = ast_.statements[i];
      if (const auto* staged = std::get_if<SourceStagedCall>(&statement.operation)) {
        if (staged->first_argument_statement.value != i + 1 ||
            staged->call_statement.value < i + 1 ||
            staged->call_statement.value >= end)
          throw SourceFailure{SourceError::kOutput, statement.pc};
        Statement(statement);
        i = std::size_t(staged->call_statement.value) + 1;
      } else {
        Statement(statement);
        ++i;
      }
    }
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
            Statements(block.first_instruction, end);
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
  std::size_t call_depth_ = 0;
  CaptureMode capture_mode_;
  std::vector<SourceCapture> captures_;
  const analysis::SemanticProof* proof_;
  CreationExpansion expansion_;
};
}  // namespace

std::variant<PrintedSource, SourceFailure> PrintSourceAst(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, std::size_t output_bytes,
    const analysis::SemanticProof* proof) {
  return SourcePrinter(ast, method, frame, form, name, constants,
                       output_bytes, SourcePrinter::CaptureMode::kReject, proof).Print();
}
std::variant<PrintedSource, SourceFailure> PrintSourceBody(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes, const analysis::SemanticProof* proof) {
  const abc::FrameLayout unused_frame{};
  return SourcePrinter(ast, method, unused_frame, SourceForm::kFunction,
                       {}, constants, output_bytes,
                       SourcePrinter::CaptureMode::kReject, proof).PrintBody(names);
}
std::variant<PrintedTemplate, SourceFailure> PrintSourceTemplate(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes, const analysis::SemanticProof* proof) {
  const abc::FrameLayout unused_frame{};
  return SourcePrinter(ast, method, unused_frame, SourceForm::kFunction,
                       {}, constants, output_bytes,
                       SourcePrinter::CaptureMode::kTemplate, proof).PrintTemplate(names);
}
}  // namespace abcd::source
