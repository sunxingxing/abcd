#include "abcd/source/semantic_source.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include "abcd/abc/pool_reference.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/string_literal.h"
#include "abcd/source/module_exports.h"
#include "abcd/source/module_imports.h"
#include "abcd/source/created_class.h"
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
#include "record_inputs.h"
#include "class_assembly.h"

namespace abcd::source {
namespace {
using analysis::Accumulator;
using analysis::ScalarExpression;
using analysis::ScalarValue;

class SourcePrinter {
 public:
  enum class CaptureMode { kReject, kTemplate };
  SourcePrinter(const SourceAst& ast, const analysis::NormalFlow& blocks,
                const abc::FrameLayout& frame, SourceForm form,
                std::string_view name, const MethodConstants& constants,
                std::size_t bytes, CaptureMode captures = CaptureMode::kReject,
                const analysis::SemanticProof* proof = nullptr,
                const analysis::SemanticMethod* capture_method = nullptr,
                const analysis::EnvironmentFlow* capture_environments = nullptr,
                const analysis::ExceptionLocals* capture_locals = nullptr,
                ModuleExports* record_exports = nullptr)
      : ast_(ast), blocks_(blocks), capture_method_(capture_method), frame_(frame), form_(form), name_(name),
        constants_(constants), limit_(bytes), capture_mode_(captures), proof_(proof),
        expansion_(constants.method, bytes, kDefaultMethodWork),
        capture_environments_(capture_environments), capture_locals_(capture_locals),
        record_exports_(record_exports) {}

  std::variant<PrintedTemplate, SourceFailure> PrintTemplate(ScalarNames names) {
    auto printed = PrintBody(names);
    if (const auto* failure = std::get_if<SourceFailure>(&printed)) return *failure;
    auto& source = std::get<PrintedSource>(printed);
    return PrintedTemplate{std::move(source.text), std::move(source.spans),
                            std::move(captures_), source.contract};
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
      Root();
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
      // This standalone wrapper API has no async form. Only an owning async
      // callable may consume a recovered await via PrintBody/PrintTemplate.
      if (!constants_.awaits.empty())
        return SourceFailure{SourceError::kUnsupportedOperation, {}};
      if (const auto failure = VerifyOwnFields(ast_, constants_)) return *failure;
      const bool record = form_ == SourceForm::kRecord;
      if (record != (record_exports_ != nullptr))
        return SourceFailure{SourceError::kInvalidEffect, {}};
      if (record && (!constants_.module || !constants_.global_scope ||
          constants_.global_scope->module_exports != record_exports_ ||
          constants_.global_scope->module_slots != record_exports_->slot_count() ||
          frame_.parameters || ast_.regions.catches))
        return SourceFailure{SourceError::kInvalidEffect, {}};
      if (record) {
        if (!capture_method_)
          return SourceFailure{SourceError::kInvalidEffect, {}};
        if (const auto failure = VerifyRecordInputs(*capture_method_, ast_, limit_))
          return *failure;
      }
      if (record)
        for (std::size_t slot = 0; slot < record_exports_->slot_count(); ++slot)
          if (record_exports_->IsInitialized(
                  analysis::ModuleSlot{static_cast<std::uint16_t>(slot)}))
            return SourceFailure{SourceError::kInvalidEffect, {}};
      if (!record && !IsSourceIdentifier(name_))
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
      if (record) {
        ClassBuffer buffer(*this);
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        const auto imports = WriteModuleImports(
            constants_.file, constants_.header, *constants_.module, stream,
            {limit_ - result_.text.size()}, constants_.string_budget);
        if (const auto* failure = std::get_if<ModuleImportFailure>(&imports))
          return SourceFailure{
              failure->kind == ModuleImportError::kResourceLimit ||
                      failure->kind == ModuleImportError::kOutputLimit
                  ? SourceError::kResourceLimit : SourceError::kConstant, {}};
        const auto aliases = record_exports_->WriteAliases(
            stream, {limit_ - result_.text.size()});
        if (const auto* failure = std::get_if<ModuleExportError>(&aliases))
          return SourceFailure{
              *failure == ModuleExportError::kResourceLimit ||
                      *failure == ModuleExportError::kOutputLimit
                  ? SourceError::kResourceLimit : SourceError::kConstant, {}};
      } else if (form_ == SourceForm::kClassConstructor) {
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
      Text(record ? "let $acc;\n" : ") {\n\"use strict\";\nlet $acc;\n");
      for (std::uint32_t i = 0; i < frame_.registers; ++i) {
        Text("let ");
        Text(bindings_[i]);
        if (record) {
          // All incoming ABI values are rejected by the record owner's proof.
        } else if (frame_.function == abc::RegisterId{i}) {
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
      Root();
      if (record) {
        if (!record_exports_->Complete())
          return SourceFailure{SourceError::kConstant, current_pc_};
      } else {
        Text(form_ == SourceForm::kClassConstructor ? "}\n}\n" : "}\n");
      }
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
  class ClassBuffer : public std::streambuf, public UnpublishedOutput {
   public:
    explicit ClassBuffer(SourcePrinter& printer) : printer_(printer) {}
   private:
    std::streamsize xsputn(const char* text, std::streamsize count) override {
      if (count < 0) return 0;
      printer_.Text({text, static_cast<std::size_t>(count)});
      return count;
    }
    int_type overflow(int_type ch) override {
      if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);
      const char value = traits_type::to_char_type(ch);
      printer_.Text({&value, 1});
      return ch;
    }
    SourcePrinter& printer_;
  };
  class LeafBuffer : public CaptureBuffer, public UnpublishedOutput {
   public:
    LeafBuffer(SourcePrinter& printer, const FunctionExpression& function)
        : printer_(printer), function_(function) {}
    bool Capture(analysis::CapturedSlot capture,
                 analysis::CaptureDomain requirement) override {
      if ((!printer_.proof_ || !printer_.capture_method_) && !printer_.capture_environments_)
        throw SourceFailure{SourceError::kCreation, printer_.current_pc_};
      const auto resolved = printer_.capture_environments_
          ? ResolveCreationCapture(*printer_.capture_environments_, function_, capture,
                                   printer_.current_pc_, printer_.capture_locals_)
          : ResolveCreationCapture(*printer_.capture_method_, *printer_.proof_,
                                   function_, capture, requirement);
      if (const auto* failure = std::get_if<SourceFailure>(&resolved)) throw *failure;
      if (const auto* outer = std::get_if<analysis::CapturedSlot>(&resolved)) {
        printer_.Capture(*outer, requirement);
        return true;
      }
      // Phased topology alone cannot discharge a stronger local obligation.
      if (printer_.capture_environments_ &&
          requirement != analysis::CaptureDomain::kLanguage)
        throw SourceFailure{SourceError::kCreation, printer_.current_pc_};
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
  void Capture(analysis::CapturedSlot cell,
      analysis::CaptureDomain requirement =
          analysis::CaptureDomain::kLanguage) {
    if (capture_mode_ != CaptureMode::kTemplate)
      throw SourceFailure{SourceError::kUnsupportedOperation, current_pc_};
    // The insertion remains unresolved through every wrapper. Its cursor is
    // part of the parent creation span, even if no surrounding byte is emitted.
    if (captures_.size() >= captures_.capacity())
      throw SourceFailure{SourceError::kResourceLimit, current_pc_};
    captures_.push_back({position_, cell, requirement});
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
    if (ast_.local_cells.empty() && !ast_.regions.catches && !ast_.array_captures) return;
    const auto reserved = [&](std::string_view name) {
      return name.starts_with("$lex") || name.starts_with("$inner") ||
             (ast_.array_captures && name.starts_with("$element")) ||
             (ast_.regions.catches && name == "$caught");
    };
    if (reserved(names_.accumulator) || reserved(names_.current_function) ||
        reserved(name_)) throw SourceFailure{SourceError::kOutput, {}};
    if (names_.rest && reserved(names_.rest->name))
      throw SourceFailure{SourceError::kOutput, {}};
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
    if (ast_.regions.catches && constants_.global_scope) {
      const auto bindings = constants_.global_scope->enclosing_bindings;
      if (std::binary_search(bindings.begin(), bindings.end(), std::string_view{"$caught"}))
        throw SourceFailure{SourceError::kOutput, {}};
    }
    if (ast_.array_captures && constants_.global_scope) {
      const auto bindings = constants_.global_scope->enclosing_bindings;
      const auto first = std::lower_bound(
          bindings.begin(), bindings.end(), std::string_view{"$element"});
      if (first != bindings.end() && first->starts_with("$element"))
        throw SourceFailure{SourceError::kOutput, {}};
    }
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
    } else if (const auto* awaited = std::get_if<analysis::AwaitValue>(&expression)) {
      // Argument staging uses a synchronous IIFE. Adding async there changes
      // promise/job ordering, so refuse rather than print an invalid await.
      if (call_depth_ || constants_.awaits.empty())
        throw SourceFailure{SourceError::kUnsupportedOperation, current_pc_};
      Text("await (");
      Value(awaited->input);
      Text(")");
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
    } else if (std::holds_alternative<MethodExpression>(expression) ||
               std::holds_alternative<AccessorExpression>(expression)) {
      // Native syntax needs an unescaped class/home-object assembly proof.
      // Keeping a typed AST node does not authorize changing function kind,
      // descriptor attributes, key conversion, naming or exception order.
      throw SourceFailure{SourceError::kClassAssembly, current_pc_};
    } else if (const auto* created = std::get_if<ClassExpression>(&expression)) {
      if (!constants_.leaf_methods ||
          created->creation.parent.value >= names_.registers.size())
        throw SourceFailure{SourceError::kCreation, current_pc_};
      // Exact Hole heritage is represented by absent extends, never undefined.
      // Attached members carry the AST's unobserved-assembly proof, with pool
      // keys checked by the adapter.
      // The class owner rejects unresolved outer captures, so this stream
      // deliberately cannot carry template capture tokens.
      ClassBuffer buffer(*this);
      std::ostream stream(&buffer);
      stream.exceptions(std::ios::badbit | std::ios::failbit);
      const auto remaining = limit_ - result_.text.size();
      std::optional<ClassParent> parent;
      switch (created->heritage) {
        case analysis::HeritageKind::kUnknown:
          parent = BoundClassParent{names_.registers[created->creation.parent.value]};
          break;
        case analysis::HeritageKind::kAbsent: break;
        case analysis::HeritageKind::kNull:
          throw SourceFailure{SourceError::kCreation, current_pc_};
      }
      const auto emitted = WriteAssembledClass(
          *created, constants_, parent,
          stream, {remaining, analysis::kDefaultSemanticWork, {remaining}},
          constants_.expansion ? constants_.expansion : &expansion_);
      if (!std::holds_alternative<std::size_t>(emitted)) {
        const bool resource = std::visit([](const auto& failure) {
          using T = std::decay_t<decltype(failure)>;
          if constexpr (requires { T::kResourceLimit; }) {
            if (failure == T::kResourceLimit) return true;
          }
          if constexpr (requires { T::kWorkLimit; }) {
            if (failure == T::kWorkLimit) return true;
          }
          if constexpr (requires { T::kOutputLimit; }) {
            if (failure == T::kOutputLimit) return true;
          }
          if constexpr (std::is_same_v<T, BodyEmitFailure>) {
            if (failure.emit_error == EmitError::kOutputLimit) return true;
          }
          if constexpr (requires { failure.kind; }) {
            using Kind = decltype(failure.kind);
            if constexpr (requires { Kind::kResourceLimit; }) {
              if (failure.kind == Kind::kResourceLimit) return true;
            }
            if constexpr (requires { Kind::kWorkLimit; }) {
              if (failure.kind == Kind::kWorkLimit) return true;
            }
          }
          return false;
        }, emitted);
        throw SourceFailure{resource ? SourceError::kResourceLimit
                                     : SourceError::kCreation, current_pc_};
      }
    } else if (const auto* captured = std::get_if<CapturedExpression>(&expression)) {
      // A zero-width typed insertion, never a string placeholder. Parent
      // binding is mandatory even when this load's register result is unused.
      Capture(captured->cell, captured->requirement);
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
    } else if (const auto* range = std::get_if<analysis::SuperRange>(&expression)) {
      if (!names_.range_derived_constructor ||
          range->first.value > names_.registers.size() ||
          range->count > names_.registers.size() - range->first.value)
        throw SourceFailure{SourceError::kSuperInitialization, current_pc_};
      Text("super(");
      for (std::uint32_t i = 0; i < range->count; ++i) {
        if (i) Text(", ");
        Value(ScalarValue{abc::RegisterId{range->first.value + i}});
      }
      Text(")");
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
  void LoopLabel(analysis::BlockId id) {
    Text("loop_");
    Number(id.value);
  }
  void ArrayElement(analysis::InstructionId id) {
    Text("$element");
    Number(id.value);
  }
  void ArrayCapture(analysis::InstructionId id) {
    Text("const ");
    ArrayElement(id);
    Text(" = ");
    Slot(analysis::ScalarSlot{Accumulator{}});
    Text(";\n");
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
          } else if constexpr (std::is_same_v<T, SourceModuleStore>) {
            if (record_exports_) {
              if (scope_depth_ || call_depth_ ||
                  !std::holds_alternative<Accumulator>(operation.value) ||
                  operation.slot.value >= record_exports_->slot_count())
                throw SourceFailure{SourceError::kConstant, current_pc_};
              ClassBuffer buffer(*this);
              std::ostream stream(&buffer);
              stream.exceptions(std::ios::badbit | std::ios::failbit);
              const auto stored = record_exports_->WriteStore(
                  analysis::ModuleStore{operation.slot}, stream,
                  {limit_ - result_.text.size()});
              if (const auto* failure = std::get_if<ModuleExportError>(&stored))
                throw SourceFailure{
                    *failure == ModuleExportError::kResourceLimit ||
                            *failure == ModuleExportError::kOutputLimit
                        ? SourceError::kResourceLimit : SourceError::kOutput,
                    current_pc_};
              return;
            }
            // A callable can update an owner-proven initialized live binding.
            // Never turn an uninitialized VM slot into a hoisted JS let here:
            // first initialization needs a top-level declaration-site proof.
            const auto& scope = constants_.global_scope;
            if (!constants_.module || !scope || !scope->module_exports ||
                operation.slot.value >= scope->module_slots ||
                operation.slot.value >= constants_.module->local_exports.size() ||
                !scope->module_exports->IsInitialized(operation.slot))
              throw SourceFailure{SourceError::kConstant, current_pc_};
            Text("$local");
            Number(operation.slot.value);
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
          } else if constexpr (std::is_same_v<T, SourceMethodStore>) {
            throw SourceFailure{SourceError::kClassAssembly, current_pc_};
          } else if constexpr (std::is_same_v<T, SourceArrayCapture>) {
            ArrayCapture(operation.element);
          } else if constexpr (std::is_same_v<T, SourceArrayCommit>) {
            if (operation.elements.empty())
              throw SourceFailure{SourceError::kInvalidEffect, current_pc_};
            ArrayCapture(operation.elements.back());
            Slot(analysis::ScalarSlot{operation.target});
            Text(" = [");
            for (std::size_t i = 0; i < operation.elements.size(); ++i) {
              if (i) Text(", ");
              ArrayElement(operation.elements[i]);
            }
            Text("];\n");
          } else if constexpr (std::is_same_v<T, SourceIndexedStore>) {
            // Assignment can invoke an inherited setter. Array construction
            // or own-descriptor provenance is required before native emission.
            throw SourceFailure{SourceError::kOwnProperty, current_pc_};
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
          } else if constexpr (std::is_same_v<T, SourceConditionalContinue>) {
            Text("if (");
            Scalar(ScalarExpression{operation.condition});
            Text(") continue ");
            LoopLabel(operation.target);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceContinue>) {
            Text("continue ");
            LoopLabel(operation.target);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceReturn>) {
            if (record_exports_) {
              const auto* value = std::get_if<analysis::SpecialValue>(&operation.value);
              if (&statement != &ast_.statements.back() || scope_depth_ ||
                  !value || *value != analysis::SpecialValue::kUndefined)
                throw SourceFailure{SourceError::kUnsupportedOperation, current_pc_};
              return;
            }
            Text("return ");
            Value(operation.value);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceThrow>) {
            Text("throw ");
            Value(operation.value);
            Text(";\n");
          } else if constexpr (std::is_same_v<T, SourceInitializerCall>) {
            throw SourceFailure{SourceError::kClassAssembly, current_pc_};
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
            if (ast_.argument_region &&
                ast_.argument_region->call == operation.call_statement)
              ArgumentRegion(*ast_.argument_region);
            else
              Statements(operation.first_argument_statement.value,
                         operation.call_statement.value, true);
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
    if (statement.class_origin) {
      const auto index = static_cast<std::size_t>(&statement - ast_.statements.data());
      const auto origin = statement.class_origin->value;
      if (origin >= index || result_.spans.size() < index - origin)
        throw SourceFailure{SourceError::kOutput, current_pc_};
      const auto& span = result_.spans[result_.spans.size() - (index - origin)];
      if (span.pc != ast_.statements[origin].pc)
        throw SourceFailure{SourceError::kOutput, current_pc_};
      result_.spans.push_back({statement.pc, span.begin, span.end});
    } else if (statement.previous_origin) {
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
  void ArgumentRegion(const SourceArgumentRegion& region) {
    for (auto b = region.last_block.value; b > region.first_block.value; --b) {
      Label(analysis::BlockId{b});
      Text(": {\n");
    }
    for (auto b = region.first_block.value; b <= region.last_block.value; ++b) {
      const auto& block = blocks_.blocks[b];
      const auto first = b == region.first_block.value
                             ? region.property.value + 1 : block.first_instruction;
      const auto end = b == region.last_block.value
                           ? region.call.value
                           : block.first_instruction + block.instruction_count;
      const auto prior_spans = result_.spans.size();
      Statements(first, end, true);
      if (b < region.last_block.value) {
        if (ast_.fallthrough[b]) {
          Text("break ");
          Label(*ast_.fallthrough[b]);
          Text(";\n");
          if (result_.spans.size() > prior_spans)
            result_.spans.back().end = position_;
        }
        Text("}\n");
      }
    }
  }
  void Statements(std::size_t first, std::size_t end, bool argument = false) {
    if (first > end || end > ast_.statements.size())
      throw SourceFailure{SourceError::kOutput, current_pc_};
    first = std::max(first, std::min(end, root_prefix_end_));
    for (std::size_t i = first; i < end;) {
      if (!argument && ast_.argument_region &&
          i > ast_.argument_region->property.value &&
          i <= ast_.argument_region->call.value) {
        i = std::size_t(ast_.argument_region->call.value) + 1;
        continue;
      }
      const auto& statement = ast_.statements[i];
      if (const auto* staged = std::get_if<SourceStagedCall>(&statement.operation)) {
        if (staged->first_argument_statement.value != i + 1 ||
            staged->call_statement.value < i + 1 ||
            (staged->call_statement.value >= end &&
             (argument || !ast_.argument_region ||
              ast_.argument_region->call != staged->call_statement)))
          throw SourceFailure{SourceError::kOutput, statement.pc};
        Statement(statement);
        i = std::size_t(staged->call_statement.value) + 1;
      } else {
        Statement(statement);
        ++i;
      }
    }
  }
  void Root() {
    // Labels have no evaluation effect, but create lexical scopes. Keep the
    // unconditional entry prefix outside them so future record declarations
    // can retain module scope. Never extract a repeated loop entry or a try
    // body; do not split staged calls or the entry block's final instruction.
    std::size_t end = 0;
    if (!ast_.regions.catches && blocks_.blocks.size() > 1 &&
        ast_.regions.order_positions.size() == blocks_.blocks.size() &&
        ast_.regions.order_positions.front() == 0) {
      const auto& entry = blocks_.blocks.front();
      bool entered_again = false;
      for (const auto& block : blocks_.blocks) {
        if (block.successor_count > block.successors.size())
          throw SourceFailure{SourceError::kInvalidEffect, block.begin};
        for (std::size_t edge = 0; edge < block.successor_count; ++edge)
          entered_again |= block.successors[edge].target.value == 0;
      }
      if (!entered_again && entry.first_instruction == 0 &&
          entry.instruction_count > 1) {
        end = entry.instruction_count - 1;
        if (end > ast_.statements.size())
          throw SourceFailure{SourceError::kInvalidEffect, entry.begin};
        if (ast_.argument_region)
          end = std::min(end, std::size_t(ast_.argument_region->property.value));
        for (std::size_t i = 0; i < end; ++i) {
          if (std::holds_alternative<SourceStagedCall>(ast_.statements[i].operation)) {
            end = i;
            break;
          }
        }
      }
    }
    Statements(0, end);
    root_prefix_end_ = end;
    Region(ast_.regions.root);
  }
  void Region(const analysis::RegionNode& node) {
    std::visit(
        [&](const auto& region) {
          using T = std::decay_t<decltype(region)>;
          if constexpr (std::is_same_v<T, analysis::RegionBlock>) {
            if (region.id.value >= blocks_.blocks.size())
              throw SourceFailure{SourceError::kOutput, current_pc_};
            const auto& block = blocks_.blocks[region.id.value];
            const auto end = std::uint64_t(block.first_instruction) +
                             block.instruction_count;
            if (end > ast_.statements.size())
              throw SourceFailure{SourceError::kOutput, current_pc_};
            Statements(block.first_instruction, end);
            if (region.id.value >= ast_.fallthrough.size())
              throw SourceFailure{SourceError::kOutput, current_pc_};
            const bool consumed_terminator = ast_.argument_region &&
                end > ast_.argument_region->property.value &&
                end <= ast_.argument_region->call.value;
            if (ast_.fallthrough[region.id.value] && !consumed_terminator) {
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
            ++scope_depth_;
            Region(*region.body);
            --scope_depth_;
            Text("}\n");
          } else if constexpr (std::is_same_v<T, analysis::RegionSequence>) {
            for (const auto& child : region.children) Region(child);
          } else if constexpr (std::is_same_v<T, analysis::RegionLoop>) {
            LoopLabel(region.header);
            Text(": while (true) {\n");
            ++scope_depth_;
            Region(*region.body);
            --scope_depth_;
            Text("}\n");
          } else if constexpr (std::is_same_v<T, analysis::RegionTry>) {
            if (!ast_.regions.catches || !region.body || !region.handler)
              throw SourceFailure{SourceError::kInvalidEffect, current_pc_};
            Text("try {\n");
            Region(*region.body);
            if (region.skip) {
              if (region.skip->value >= ast_.statements.size())
                throw SourceFailure{SourceError::kInvalidEffect, current_pc_};
              Statement(SourceStatement{ast_.statements[region.skip->value].pc,
                                         analysis::NoOperation{}});
            }
            Text("} catch ($caught) {\n");
            const auto caught_begin = position_;
            Slot(analysis::ScalarSlot{Accumulator{}});
            Text(" = $caught;\n");
            const auto first_span = result_.spans.size();
            Region(*region.handler);
            if (first_span == result_.spans.size())
              throw SourceFailure{SourceError::kInvalidEffect, current_pc_};
            // Catch entry is a VM phase, not an extra bytecode instruction.
            // Include installation in the first handler instruction's span.
            result_.spans[first_span].begin = caught_begin;
            Text("}\n");
          } else {
            static_assert(std::is_same_v<T, analysis::RegionTry>);
          }
        },
        node.value);
  }

  const SourceAst& ast_;
  const analysis::NormalFlow& blocks_;
  const analysis::SemanticMethod* capture_method_;
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
  std::size_t root_prefix_end_ = 0;
  std::size_t scope_depth_ = 0;
  CaptureMode capture_mode_;
  std::vector<SourceCapture> captures_;
  const analysis::SemanticProof* proof_;
  CreationExpansion expansion_;
  const analysis::EnvironmentFlow* capture_environments_;
  const analysis::ExceptionLocals* capture_locals_;
  ModuleExports* record_exports_;
};
}  // namespace

std::variant<PrintedSource, SourceFailure> PrintSourceAst(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    const abc::FrameLayout& frame, SourceForm form, std::string_view name,
    const MethodConstants& constants, std::size_t output_bytes,
    const analysis::SemanticProof* proof, ModuleExports* record_exports) {
  return SourcePrinter(ast, method.flow, frame, form, name, constants,
                       output_bytes, SourcePrinter::CaptureMode::kReject, proof,
                       &method, nullptr, nullptr, record_exports).Print();
}
std::variant<PrintedSource, SourceFailure> PrintSourceBody(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes, const analysis::SemanticProof* proof) {
  const abc::FrameLayout unused_frame{};
  return SourcePrinter(ast, method.flow, unused_frame, SourceForm::kFunction,
                       {}, constants, output_bytes,
                       SourcePrinter::CaptureMode::kReject, proof, &method).PrintBody(names);
}
std::variant<PrintedSource, SourceFailure> PrintSourceBody(
    const SourceAst& ast, const analysis::NormalFlow& blocks,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes, const analysis::EnvironmentFlow* environments,
    const analysis::ExceptionLocals* locals) {
  const abc::FrameLayout unused_frame{};
  return SourcePrinter(ast, blocks, unused_frame, SourceForm::kFunction,
                       {}, constants, output_bytes, SourcePrinter::CaptureMode::kReject,
                       nullptr, nullptr, environments, locals).PrintBody(names);
}
std::variant<PrintedTemplate, SourceFailure> PrintSourceTemplate(
    const SourceAst& ast, const analysis::SemanticMethod& method,
    ScalarNames names, const MethodConstants& constants,
    std::size_t output_bytes, const analysis::SemanticProof* proof) {
  const abc::FrameLayout unused_frame{};
  return SourcePrinter(ast, method.flow, unused_frame, SourceForm::kFunction,
                       {}, constants, output_bytes,
                       SourcePrinter::CaptureMode::kTemplate, proof, &method).PrintTemplate(names);
}
std::variant<PrintedTemplate, SourceFailure> PrintSourceTemplate(
    const SourceAst& ast, const analysis::NormalFlow& blocks,
    ScalarNames names, const MethodConstants& constants, std::size_t output_bytes,
    const analysis::EnvironmentFlow* environments, const analysis::ExceptionLocals* locals) {
  const abc::FrameLayout unused_frame{};
  return SourcePrinter(ast, blocks, unused_frame, SourceForm::kFunction,
                       {}, constants, output_bytes,
                       SourcePrinter::CaptureMode::kTemplate, nullptr, nullptr,
                       environments, locals).PrintTemplate(names);
}
}  // namespace abcd::source
