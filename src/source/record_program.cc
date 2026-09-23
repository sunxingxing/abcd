#include "abcd/source/record_program.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <type_traits>

#include "abcd/abc/call_type.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/class_creation.h"
#include "abcd/analysis/define_instruction.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/indexed_write.h"
#include "abcd/analysis/scalar.h"
#include "abcd/source/created_function.h"
#include "abcd/source/module_imports.h"
#include "abcd/source/static_fields.h"
#include "record_buffer.h"
#include "record_literal.h"

namespace abcd::source {
namespace {
using Error = RecordProgramError;
struct Value {
  enum class Kind {
    kUnknown,
    kUndefined,
    kHole,
    kClass,
    kFunction,
    kCreatedMethod,
    kString,
    kFalse,
    kTrue,
    kNull,
    kAtom,
    kLiteral,
    kPrototype
  } kind;
  std::size_t ordinal = 0;
};
struct ClassState {
  const abc::BodyInstruction* site = nullptr;
  std::optional<StaticFields> fields;
  std::vector<ClassAccessor> accessors;
  bool emitted = false;
};
struct CreatedMethodState {
  std::size_t class_id = 0;
  abc::MethodPosition method;
  abc::ClassPlacement home = abc::ClassPlacement::kPrototype;
  std::uint8_t length = 0;
  bool consumed = false;
};
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
  int_type overflow(int_type value) override {
    return traits_type::not_eof(value);
  }
};
RecordProgramFailure Fail(Error kind, abc::CodeOffset pc = {}) {
  return {kind, pc};
}
Error Metadata(abc::MetadataError error) {
  switch (error) {
    case abc::MetadataError::kResourceLimit:
      return Error::kResourceLimit;
    case abc::MetadataError::kRead:
      return Error::kRead;
    case abc::MetadataError::kFileChanged:
      return Error::kFileChanged;
    default:
      return Error::kBody;
  }
}
template <typename Result>
Error CreationError(const Result& result, Error semantic) {
  return std::visit(
      [semantic](const auto& value) -> Error {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, CreatedClassError>) {
          return value == CreatedClassError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, CreatedFunctionError>) {
          return value == CreatedFunctionError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, FunctionEmitError>) {
          return value == FunctionEmitError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, EmitError>) {
          return value == EmitError::kOutputLimit
                     ? Error::kOutputLimit
                     : value == EmitError::kWrite ? Error::kWrite
                                                  : semantic;
        } else if constexpr (std::is_same_v<T, BodyEmitFailure>) {
          return value.kind == BodyEmitError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, abc::MetadataError>) {
          return Metadata(value);
        } else if constexpr (std::is_same_v<T, abc::BodyFailure>) {
          return value.kind == abc::BodyError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, analysis::FlowFailure>) {
          return value.kind == analysis::FlowError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (
            std::is_same_v<T, analysis::EnvironmentFlowFailure>) {
          return value.kind ==
                         analysis::EnvironmentFlowError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (
            std::is_same_v<T, analysis::LinearLexicalFailure>) {
          return value.kind == analysis::LinearLexicalError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (
            std::is_same_v<T, analysis::AccumulatorFailure>) {
          return value.kind == analysis::AccumulatorError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, AcyclicError>) {
          return value == AcyclicError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, abc::LiteralFailure>) {
          return value.kind == abc::LiteralError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else if constexpr (std::is_same_v<T, abc::ClassBufferError>) {
          return value == abc::ClassBufferError::kResourceLimit
                     ? Error::kResourceLimit : semantic;
        } else {
          return semantic;
        }
      },
      result);
}
std::variant<Value, RecordProgramFailure> ScalarValue(
    const analysis::ScalarValue& source, Value accumulator,
    std::span<const Value> registers, abc::CodeOffset pc) {
  if (std::holds_alternative<analysis::Accumulator>(source))
    return accumulator;
  if (const auto* register_id = std::get_if<abc::RegisterId>(&source)) {
    if (register_id->value >= registers.size()) return Fail(Error::kFrame, pc);
    return registers[register_id->value];
  }
  if (const auto* special = std::get_if<analysis::SpecialValue>(&source);
      special && *special == analysis::SpecialValue::kUndefined)
    return Value{Value::Kind::kUndefined};
  if (const auto* special = std::get_if<analysis::SpecialValue>(&source);
      special && *special == analysis::SpecialValue::kFalse)
    return Value{Value::Kind::kFalse};
  if (const auto* special = std::get_if<analysis::SpecialValue>(&source);
      special && *special == analysis::SpecialValue::kTrue)
    return Value{Value::Kind::kTrue};
  if (const auto* special = std::get_if<analysis::SpecialValue>(&source);
      special && *special == analysis::SpecialValue::kNull)
    return Value{Value::Kind::kNull};
  return Fail(Error::kInstruction, pc);
}
class ProgramWriter {
 public:
  ProgramWriter(const corpus::AbcFile& file, const abc::Header& header,
                const abc::MetadataIndex& index,
                const abc::MethodLookup& lookup, const abc::MethodInfo& entry,
                const abc::FrameLayout& frame, const abc::MethodBody& body,
                const abc::ModuleInfo& module,
                const analysis::MethodRoleIndex& roles,
                ModuleExports exports, std::ostream& output,
                RecordProgramBudget budget)
      : file_(file), header_(header), index_(index), lookup_(lookup),
        entry_(entry), body_(body), module_(module), roles_(roles),
        exports_(std::move(exports)), output_(output), budget_(budget),
        registers_(frame.registers, Value{Value::Kind::kUnknown}),
        escaped_(body.instructions.size()) {}

  std::size_t Write() {
    if (!body_.tries.empty() || !body_.handlers.empty())
      throw Fail(Error::kInstruction);
    if (!module_.indirect_exports.empty() || !module_.star_exports.empty())
      throw Fail(Error::kModule);
    const auto imports = WriteModuleImports(file_, header_, module_, output_,
                                            {Remaining()}, budget_.strings);
    if (const auto* failure = std::get_if<ModuleImportFailure>(&imports)) {
      if (failure->metadata)
        throw Fail(Metadata(*failure->metadata));
      throw Fail(failure->kind == ModuleImportError::kResourceLimit
                     ? Error::kResourceLimit
                     : failure->kind == ModuleImportError::kOutputLimit
                           ? Error::kOutputLimit
                           : failure->kind == ModuleImportError::kWrite
                                 ? Error::kWrite
                           : Error::kModule);
    }
    Account(std::get<std::size_t>(imports));
    const auto aliases = exports_.WriteAliases(output_, {Remaining()});
    if (const auto* error = std::get_if<ModuleExportError>(&aliases))
      throw Fail(*error == ModuleExportError::kOutputLimit
                     ? Error::kOutputLimit
                     : *error == ModuleExportError::kResourceLimit
                           ? Error::kResourceLimit
                           : *error == ModuleExportError::kWrite
                                 ? Error::kWrite
                           : Error::kExport);
    Account(std::get<std::size_t>(aliases));
    Text("let $acc;\n");
    for (std::size_t i = 0; i < body_.instructions.size(); ++i) {
      const auto& instruction = body_.instructions[i];
      pc_ = instruction.offset;
      if (instruction.branch_target) throw Fail(Error::kInstruction, pc_);
      using abc::Opcode;
      switch (instruction.decoded.opcode) {
        case Opcode::kLDHOLE:
          if (instruction.decoded.operand_count != 0)
            throw Fail(Error::kInstruction, pc_);
          accumulator_ = {Value::Kind::kHole};
          break;
        case Opcode::kLDA_STR_ID16:
          String(instruction.decoded);
          break;
        case Opcode::kCREATEEMPTYOBJECT:
          EmptyObject(instruction.decoded);
          break;
        case Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16:
        case Opcode::kCREATEOBJECTWITHBUFFER_IMM16_ID16:
          BufferedObject(instruction.decoded);
          break;
        case Opcode::kCREATEEMPTYARRAY_IMM8:
        case Opcode::kCREATEEMPTYARRAY_IMM16:
          EmptyArray(instruction.decoded);
          break;
        case Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8:
        case Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8:
          DefineObjectField(instruction.decoded);
          break;
        case Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16:
        case Opcode::kSTOWNBYINDEX_IMM16_V8_IMM16:
        case Opcode::kWIDE_STOWNBYINDEX_PREF_V8_IMM32:
          StoreArrayElement(instruction.decoded);
          break;
        case Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8:
        case Opcode::kDEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8:
          Class(instruction);
          break;
        case Opcode::kDEFINEFUNC_IMM8_ID16_IMM8:
        case Opcode::kDEFINEFUNC_IMM16_ID16_IMM8:
          Function(instruction);
          break;
        case Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8:
        case Opcode::kDEFINEMETHOD_IMM16_ID16_IMM8:
          DefineMethod(instruction);
          break;
        case Opcode::kCALLTHIS0_IMM8_V8:
          CallInitializer(instruction);
          break;
        case Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8:
          DefineAccessor(instruction);
          break;
        case Opcode::kLDOBJBYNAME_IMM8_ID16:
        case Opcode::kLDOBJBYNAME_IMM16_ID16:
          Prototype(instruction);
          break;
        case Opcode::kSTMODULEVAR_IMM8:
        case Opcode::kWIDE_STMODULEVAR_PREF_IMM16:
          Store(instruction);
          break;
        case Opcode::kRETURNUNDEFINED:
          if (i + 1 != body_.instructions.size() ||
              instruction.decoded.operand_count != 0)
            throw Fail(Error::kInstruction, pc_);
          for (const auto& created : classes_)
            if (!created.emitted) throw Fail(Error::kClass, pc_);
          for (const auto& method : created_methods_)
            if (!method.consumed) throw Fail(Error::kInitializer, pc_);
          for (const auto& node : nodes_)
            if (!node.attached && !node.emitted)
              throw Fail(Error::kObject, pc_);
          if (!exports_.Complete()) throw Fail(Error::kExport, pc_);
          return written_;
        case Opcode::kNOP:
          break;
        default:
          Scalar(instruction);
          break;
      }
    }
    throw Fail(Error::kInstruction, pc_);
  }

 private:
  template <typename T>
  void Append(std::vector<T>& values, T value) {
    if (values.size() == values.capacity()) {
      const auto cap = values.capacity();
      constexpr std::size_t kInitialCapacity = 4;
      const auto maximum = body_.instructions.size();
      const auto next = cap == 0 ? std::min(kInitialCapacity, maximum)
                                 : cap > maximum / 2 ? maximum : cap * 2;
      if (next <= cap || (next - cap) > budget_.bytes / sizeof(T))
        throw Fail(Error::kResourceLimit, pc_);
      values.reserve(next);
      const auto added = values.capacity() - cap;
      if (added > budget_.bytes / sizeof(T))
        throw Fail(Error::kResourceLimit, pc_);
      budget_.bytes -= added * sizeof(T);
    }
    values.push_back(std::move(value));
  }
  std::size_t Remaining() const { return budget_.output.bytes - written_; }
  void Account(std::size_t bytes) {
    if (bytes > Remaining()) throw Fail(Error::kOutputLimit, pc_);
    written_ += bytes;
  }
  void Text(std::string_view text) {
    if (text.size() > Remaining()) throw Fail(Error::kOutputLimit, pc_);
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) throw Fail(Error::kWrite, pc_);
    written_ += text.size();
  }
  void Number(std::size_t value) {
    std::array<char, std::numeric_limits<std::size_t>::digits10 + 2> digits{};
    const auto encoded = std::to_chars(digits.data(), digits.data() +
                                      digits.size(), value);
    if (encoded.ec != std::errc{}) throw Fail(Error::kResourceLimit, pc_);
    Text({digits.data(), static_cast<std::size_t>(encoded.ptr - digits.data())});
  }
  void Scalar(const abc::BodyInstruction& instruction) {
    const auto lifted = analysis::LiftScalarInstruction(
        instruction.decoded, registers_.size());
    const auto* assignment = std::get_if<analysis::ScalarAssignment>(&lifted);
    if (!assignment ||
        assignment->completion != analysis::CompletionEffect::kNormal)
      throw Fail(Error::kInstruction, pc_);
    const auto* value = std::get_if<analysis::ScalarValue>(
        &assignment->expression);
    if (!value) throw Fail(Error::kInstruction, pc_);
    const auto resolved = [&]() -> std::variant<Value, RecordProgramFailure> {
      if (const auto* integer = std::get_if<analysis::IntegerLiteral>(value))
        return SaveAtom(integer->value);
      if (const auto* number = std::get_if<analysis::Float64Bits>(value))
        return SaveAtom(StaticFloat{number->value});
      return ScalarValue(*value, accumulator_, registers_, pc_);
    }();
    if (const auto* failure = std::get_if<RecordProgramFailure>(&resolved))
      throw *failure;
    if (std::holds_alternative<analysis::Accumulator>(
            assignment->destination))
      accumulator_ = std::get<Value>(resolved);
    else {
      const auto register_id = std::get<abc::RegisterId>(
          assignment->destination);
      if (register_id.value >= registers_.size())
        throw Fail(Error::kFrame, pc_);
      registers_[register_id.value] = std::get<Value>(resolved);
    }
  }
  void String(const abc::DecodedInstruction& instruction) {
    if (instruction.operand_count != 1 ||
        instruction.operands[0].kind != abc::OperandKind::kString ||
        instruction.operands[0].raw_bits > UINT16_MAX)
      throw Fail(Error::kInstruction, pc_);
    accumulator_ = {Value::Kind::kString,
                    static_cast<std::size_t>(instruction.operands[0].raw_bits)};
  }
  Value SaveAtom(StaticAtom atom) {
    if (const auto* text = std::get_if<std::u16string>(&atom)) {
      if (text->capacity() > budget_.bytes / sizeof(char16_t))
        throw Fail(Error::kResourceLimit, pc_);
      budget_.bytes -= text->capacity() * sizeof(char16_t);
    }
    const auto id = atoms_.size();
    Append(atoms_, std::move(atom));
    return {Value::Kind::kAtom, id};
  }
  std::u16string ReadPoolString(abc::PoolIndex index) {
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, entry_.offset, index,
        abc::PoolEntityKind::kString);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      throw Fail(Metadata(*error), pc_);
    const auto decoded = abc::ReadStringEntity(
        file_, header_, std::get<abc::PoolReference>(reference).offset,
        {std::min(budget_.strings.bytes, budget_.bytes)});
    if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
      throw Fail(Metadata(*error), pc_);
    return std::get<std::u16string>(std::move(decoded));
  }
  void EmptyObject(const abc::DecodedInstruction& instruction) {
    if (!std::holds_alternative<analysis::EmptyObjectCreation>(
            analysis::LiftObjectCreation(instruction)))
      throw Fail(Error::kObject, pc_);
    NewLiteral(RecordLiteralKind::kObject);
  }
  void BufferedObject(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftObjectCreation(instruction);
    const auto* creation =
        std::get_if<analysis::BufferedObjectCreation>(&lifted);
    if (!creation) throw Fail(Error::kObject, pc_);
    auto decoded = ReadRecordObjectBuffer(
        file_, header_, index_, entry_.offset, creation->literal,
        budget_.bytes, budget_.strings);
    if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
      throw Fail(Metadata(*error), pc_);
    if (const auto* error = std::get_if<abc::LiteralFailure>(&decoded))
      throw Fail(error->kind == abc::LiteralError::kResourceLimit
                     ? Error::kResourceLimit
                     : error->kind == abc::LiteralError::kRead
                           ? Error::kRead
                           : error->kind == abc::LiteralError::kFileChanged
                                 ? Error::kFileChanged
                                 : Error::kObject,
                 pc_);
    if (const auto* error = std::get_if<RecordBufferError>(&decoded))
      throw Fail(*error == RecordBufferError::kResourceLimit
                     ? Error::kResourceLimit : Error::kObject,
                 pc_);
    auto buffer = std::get<BufferedRecordObject>(std::move(decoded));
    if (buffer.retained_bytes > budget_.bytes)
      throw Fail(Error::kResourceLimit, pc_);
    budget_.bytes -= buffer.retained_bytes;
    NewLiteral(RecordLiteralKind::kObject);
    nodes_[accumulator_.ordinal].fields = std::move(buffer.fields);
  }
  void EmptyArray(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftScalarInstruction(
        instruction, registers_.size());
    const auto* assignment = std::get_if<analysis::ScalarAssignment>(&lifted);
    const auto* value = assignment
                            ? std::get_if<analysis::ScalarValue>(
                                  &assignment->expression)
                            : nullptr;
    if (!value || !std::holds_alternative<analysis::EmptyArray>(*value))
      throw Fail(Error::kObject, pc_);
    NewLiteral(RecordLiteralKind::kArray);
  }
  void NewLiteral(RecordLiteralKind kind) {
    const auto id = nodes_.size();
    Append(nodes_, RecordLiteralNode{kind});
    accumulator_ = {Value::Kind::kLiteral, id};
  }
  RecordLiteralValue LiteralValue(std::size_t parent) {
    switch (accumulator_.kind) {
      case Value::Kind::kUndefined:
        return StaticAtom{StaticUndefined{}};
      case Value::Kind::kNull:
        return StaticAtom{StaticNull{}};
      case Value::Kind::kFalse:
        return StaticAtom{false};
      case Value::Kind::kTrue:
        return StaticAtom{true};
      case Value::Kind::kString:
        return StaticAtom{ReadPoolString({static_cast<std::uint32_t>(
            accumulator_.ordinal)})};
      case Value::Kind::kAtom:
        if (accumulator_.ordinal >= atoms_.size()) break;
        return atoms_[accumulator_.ordinal];
      case Value::Kind::kLiteral: {
        const auto child = accumulator_.ordinal;
        if (child <= parent || child >= nodes_.size() ||
            nodes_[child].attached || nodes_[child].emitted)
          break;
        return RecordNodeRef{child};
      }
      default:
        break;
    }
    throw Fail(Error::kObject, pc_);
  }
  void ChargeValue(const RecordLiteralValue& value) {
    const auto* atom = std::get_if<StaticAtom>(&value);
    if (!atom) return;
    const auto* text = std::get_if<std::u16string>(atom);
    if (!text) return;
    if (text->capacity() > budget_.bytes / sizeof(char16_t))
      throw Fail(Error::kResourceLimit, pc_);
    budget_.bytes -= text->capacity() * sizeof(char16_t);
  }
  void DefineObjectField(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftOwnWrite(instruction, registers_.size());
    const auto* write = std::get_if<analysis::OwnWrite>(&lifted);
    if (!write || write->kind != analysis::OwnWriteKind::kDefineField)
      throw Fail(Error::kObject, pc_);
    const auto object = registers_[write->object.value];
    if (object.kind != Value::Kind::kLiteral ||
        object.ordinal >= nodes_.size() ||
        nodes_[object.ordinal].kind != RecordLiteralKind::kObject ||
        nodes_[object.ordinal].attached || nodes_[object.ordinal].emitted)
      throw Fail(Error::kObject, pc_);
    RecordLiteralField field{ReadPoolString(write->name),
                             LiteralValue(object.ordinal)};
    if (field.key.capacity() > budget_.bytes / sizeof(char16_t))
      throw Fail(Error::kResourceLimit, pc_);
    budget_.bytes -= field.key.capacity() * sizeof(char16_t);
    ChargeValue(field.value);
    const auto child = std::get_if<RecordNodeRef>(&field.value);
    const auto child_id = child ? std::optional(child->id) : std::nullopt;
    Append(nodes_[object.ordinal].fields, std::move(field));
    if (child_id) nodes_[*child_id].attached = true;
  }
  void StoreArrayElement(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftIndexedWrite(
        instruction, registers_.size());
    const auto* write = std::get_if<analysis::IndexedWrite>(&lifted);
    if (!write) throw Fail(Error::kObject, pc_);
    const auto array = registers_[write->object.value];
    if (array.kind != Value::Kind::kLiteral ||
        array.ordinal >= nodes_.size() ||
        nodes_[array.ordinal].kind != RecordLiteralKind::kArray ||
        nodes_[array.ordinal].attached || nodes_[array.ordinal].emitted ||
        write->index != nodes_[array.ordinal].elements.size())
      throw Fail(Error::kObject, pc_);
    auto value = LiteralValue(array.ordinal);
    ChargeValue(value);
    const auto child = std::get_if<RecordNodeRef>(&value);
    const auto child_id = child ? std::optional(child->id) : std::nullopt;
    Append(nodes_[array.ordinal].elements, std::move(value));
    if (child_id) nodes_[*child_id].attached = true;
  }
  void EmitLiteral(std::size_t id) {
    if (id >= nodes_.size() || nodes_[id].attached)
      throw Fail(Error::kObject, pc_);
    if (nodes_[id].emitted) return;
    Text("const $literal");
    Number(id);
    Text(" = ");
    const auto result = WriteRecordLiteral(nodes_, id, output_, {Remaining()});
    if (const auto* error = std::get_if<EmitError>(&result))
      throw Fail(*error == EmitError::kOutputLimit
                     ? Error::kOutputLimit
                     : *error == EmitError::kWrite ? Error::kWrite
                                                   : Error::kObject,
                 pc_);
    if (std::holds_alternative<RecordLiteralError>(result))
      throw Fail(Error::kObject, pc_);
    Account(std::get<std::size_t>(result));
    Text(";\n");
    nodes_[id].emitted = true;
  }
  void Class(const abc::BodyInstruction& instruction) {
    const auto lifted = analysis::LiftClassCreation(instruction.decoded);
    const auto* creation = std::get_if<analysis::ClassCreation>(&lifted);
    if (!creation || creation->parent.value >= registers_.size() ||
        registers_[creation->parent.value].kind != Value::Kind::kHole)
      throw Fail(Error::kValue, pc_);
    // Keep the creation site until the fresh class's one proved initializer
    // has run. Emitting earlier would omit its fields from the class body.
    const auto id = classes_.size();
    Append(classes_, ClassState{&instruction});
    accumulator_ = {Value::Kind::kClass, id};
  }
  void EmitClass(std::size_t id) {
    if (id >= classes_.size() || classes_[id].emitted)
      throw Fail(Error::kValue, pc_);
    Text("const $class");
    Number(id);
    Text(" = ");
    LocalLexicalBudget child;
    child.bytes = budget_.bytes;
    child.output.bytes = Remaining();
    const GlobalScope scope{{}, {}};
    const auto result = WriteCreatedClass(
        file_, header_, index_, lookup_, entry_.offset, *classes_[id].site,
        {classes_[id].site->offset, analysis::HeritageKind::kAbsent}, output_,
        child, budget_.strings, &roles_, &module_, &scope,
        classes_[id].fields ? &*classes_[id].fields : nullptr,
        classes_[id].accessors);
    if (const auto* size = std::get_if<std::size_t>(&result))
      Account(*size);
    else
      throw Fail(CreationError(result, Error::kClass), pc_);
    Text(";\n");
    classes_[id].emitted = true;
  }
  void Function(const abc::BodyInstruction& instruction) {
    const auto id = function_count_++;
    Text("const $function");
    Number(id);
    Text(" = ");
    LocalLexicalBudget child;
    child.bytes = budget_.bytes;
    child.output.bytes = Remaining();
    const GlobalScope scope{{}, {}};
    const auto result = WriteCreatedFunction(
        file_, header_, index_, lookup_, entry_.offset, instruction.decoded,
        output_, child, budget_.strings, &module_, &roles_, &scope);
    if (const auto* size = std::get_if<std::size_t>(&result))
      Account(*size);
    else
      throw Fail(CreationError(result, Error::kFunction), pc_);
    Text(";\n");
    accumulator_ = {Value::Kind::kFunction, id};
  }
  void DefineMethod(const abc::BodyInstruction& instruction) {
    const auto lifted = analysis::LiftFunctionCreation(instruction.decoded);
    const auto* creation = std::get_if<analysis::FunctionCreation>(&lifted);
    if (!creation || creation->kind != analysis::CreationKind::kMethod ||
        creation->home_object != analysis::HomeObjectInput::kAccumulator ||
        creation->length.value > 1 ||
        (accumulator_.kind != Value::Kind::kPrototype &&
         accumulator_.kind != Value::Kind::kClass) ||
        accumulator_.ordinal >= classes_.size() ||
        escaped_[accumulator_.ordinal])
      throw Fail(Error::kInitializer, pc_);
    const auto target = abc::ResolveMethodTarget(
        file_, header_, index_, lookup_, entry_.offset, creation->method);
    if (const auto* error = std::get_if<abc::MetadataError>(&target))
      throw Fail(Metadata(*error), pc_);
    const auto* method = std::get_if<abc::MethodTarget>(&target);
    if (!method || method->position.value >= index_.methods.size() ||
        method->attributes.sendable ||
        method->attributes.kind != abc::FunctionKind::kNone ||
        index_.methods[method->position.value].owner_offset !=
            entry_.owner_offset)
      throw Fail(Error::kInitializer, pc_);
    const auto id = created_methods_.size();
    Append(created_methods_, CreatedMethodState{
                                 accumulator_.ordinal, method->position,
                                 accumulator_.kind == Value::Kind::kClass
                                     ? abc::ClassPlacement::kStatic
                                     : abc::ClassPlacement::kPrototype,
                                 creation->length.value});
    accumulator_ = {Value::Kind::kCreatedMethod, id};
  }
  void CallInitializer(const abc::BodyInstruction& instruction) {
    const auto& decoded = instruction.decoded;
    if (decoded.operand_count != 2 ||
        decoded.operands[0].kind != abc::OperandKind::kImmediate ||
        decoded.operands[0].raw_bits > UINT8_MAX ||
        decoded.operands[1].kind != abc::OperandKind::kRegister ||
        decoded.operands[1].raw_bits > UINT8_MAX ||
        decoded.operands[1].raw_bits >= registers_.size() ||
        accumulator_.kind != Value::Kind::kCreatedMethod ||
        accumulator_.ordinal >= created_methods_.size())
      throw Fail(Error::kInitializer, pc_);
    auto& init = created_methods_[accumulator_.ordinal];
    const auto class_id = init.class_id;
    const auto& receiver = registers_[decoded.operands[1].raw_bits];
    if (init.consumed || init.length != 0 ||
        init.home != abc::ClassPlacement::kStatic ||
        class_id >= classes_.size() ||
        receiver.kind != Value::Kind::kClass ||
        receiver.ordinal != class_id || escaped_[class_id] ||
        classes_[class_id].emitted || classes_[class_id].fields)
      throw Fail(Error::kInitializer, pc_);
    const auto recovered = RecoverStaticFields(
        file_, header_, index_, index_.methods[init.method.value],
        budget_.bytes, budget_.strings);
    if (const auto* failure = std::get_if<StaticFieldFailure>(&recovered)) {
      const auto mapped = failure->kind == StaticFieldError::kResourceLimit
                              ? Error::kResourceLimit
                          : failure->kind == StaticFieldError::kRead
                              ? Error::kRead
                          : failure->kind == StaticFieldError::kFileChanged
                              ? Error::kFileChanged
                              : Error::kInitializer;
      throw Fail(mapped, failure->pc);
    }
    auto fields = std::get<StaticFields>(std::move(recovered));
    const auto retained = fields.AllocatedBytes();
    if (retained > budget_.bytes) throw Fail(Error::kResourceLimit, pc_);
    budget_.bytes -= retained;
    classes_[class_id].fields = std::move(fields);
    init.consumed = true;
    accumulator_ = {Value::Kind::kUndefined};
  }
  void DefineAccessor(const abc::BodyInstruction& instruction) {
    const auto lifted = analysis::LiftDefineInstruction(
        instruction.decoded, registers_.size());
    const auto* define = std::get_if<analysis::DefineAccessor>(&lifted);
    if (!define || accumulator_.kind != Value::Kind::kFalse)
      throw Fail(Error::kAccessor, pc_);
    const auto object = registers_[define->object.value];
    const auto key = registers_[define->key.value];
    const auto getter = registers_[define->getter.value];
    const auto setter = registers_[define->setter.value];
    const bool has_getter = getter.kind == Value::Kind::kCreatedMethod &&
                            setter.kind == Value::Kind::kUndefined;
    const bool has_setter = setter.kind == Value::Kind::kCreatedMethod &&
                            getter.kind == Value::Kind::kUndefined;
    if ((object.kind != Value::Kind::kClass &&
         object.kind != Value::Kind::kPrototype) ||
        key.kind != Value::Kind::kString ||
        (!has_getter && !has_setter) ||
        object.ordinal >= classes_.size() ||
        escaped_[object.ordinal] || classes_[object.ordinal].emitted ||
        classes_[object.ordinal].fields)
      throw Fail(Error::kAccessor, pc_);
    const auto method_id = has_getter ? getter.ordinal : setter.ordinal;
    if (method_id >= created_methods_.size())
      throw Fail(Error::kAccessor, pc_);
    auto& method = created_methods_[method_id];
    const auto placement = object.kind == Value::Kind::kClass
                               ? abc::ClassPlacement::kStatic
                               : abc::ClassPlacement::kPrototype;
    if (method.consumed || method.class_id != object.ordinal ||
        method.home != placement ||
        method.length != (has_getter ? 0 : 1) ||
        roles_.At(method.method) != analysis::MethodRole::kUnseen)
      throw Fail(Error::kAccessor, pc_);
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, entry_.offset,
        {static_cast<std::uint32_t>(key.ordinal)},
        abc::PoolEntityKind::kString);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      throw Fail(Metadata(*error), pc_);
    auto decoded = abc::ReadStringEntity(
        file_, header_, std::get<abc::PoolReference>(reference).offset,
        budget_.strings);
    if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
      throw Fail(Metadata(*error), pc_);
    auto name = std::get<std::u16string>(std::move(decoded));
    if (name.capacity() > budget_.bytes / sizeof(char16_t))
      throw Fail(Error::kResourceLimit, pc_);
    budget_.bytes -= name.capacity() * sizeof(char16_t);
    auto& accessors = classes_[object.ordinal].accessors;
    Append(accessors, ClassAccessor{
                          std::move(name), method.method, placement,
                          has_getter ? abc::ClassMethodKind::kGetter
                                     : abc::ClassMethodKind::kSetter,
                          method.length});
    method.consumed = true;
    accumulator_ = object;
  }
  void Prototype(const abc::BodyInstruction& instruction) {
    const auto read = analysis::LiftNamedRead(instruction.decoded);
    const auto* named = std::get_if<analysis::NamedRead>(&read);
    if (!named || accumulator_.kind != Value::Kind::kClass ||
        accumulator_.ordinal >= classes_.size() ||
        escaped_[accumulator_.ordinal])
      throw Fail(Error::kValue, pc_);
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, entry_.offset, named->name,
        abc::PoolEntityKind::kString);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      throw Fail(Metadata(*error), pc_);
    const auto name = abc::ReadStringEntity(
        file_, header_, std::get<abc::PoolReference>(reference).offset,
        budget_.strings);
    if (const auto* error = std::get_if<abc::MetadataError>(&name))
      throw Fail(Metadata(*error), pc_);
    // Pinned native class expressions own a non-accessor prototype property;
    // this read is pure only before the fresh class has escaped.
    if (std::get<std::u16string>(name) != u"prototype")
      throw Fail(Error::kInstruction, pc_);
    accumulator_ = {Value::Kind::kPrototype, accumulator_.ordinal};
  }
  void Store(const abc::BodyInstruction& instruction) {
    const auto lifted = analysis::LiftModuleStore(instruction.decoded);
    const auto* store = std::get_if<analysis::ModuleStore>(&lifted);
    if (!store || (accumulator_.kind != Value::Kind::kClass &&
                   accumulator_.kind != Value::Kind::kFunction &&
                   accumulator_.kind != Value::Kind::kLiteral &&
                   accumulator_.kind != Value::Kind::kUndefined))
      throw Fail(Error::kValue, pc_);
    if (accumulator_.kind == Value::Kind::kClass) {
      if (accumulator_.ordinal >= classes_.size())
        throw Fail(Error::kValue, pc_);
      if (!classes_[accumulator_.ordinal].emitted)
        EmitClass(accumulator_.ordinal);
    }
    if (accumulator_.kind == Value::Kind::kLiteral)
      EmitLiteral(accumulator_.ordinal);
    Text("$acc = ");
    if (accumulator_.kind == Value::Kind::kClass) {
      Text("$class");
      Number(accumulator_.ordinal);
    } else if (accumulator_.kind == Value::Kind::kFunction) {
      Text("$function");
      Number(accumulator_.ordinal);
    } else if (accumulator_.kind == Value::Kind::kLiteral) {
      Text("$literal");
      Number(accumulator_.ordinal);
    } else {
      Text("void 0");
    }
    Text(";\n");
    const auto result = exports_.WriteStore(*store, output_, {Remaining()});
    if (const auto* error = std::get_if<ModuleExportError>(&result))
      throw Fail(*error == ModuleExportError::kOutputLimit
                     ? Error::kOutputLimit
                     : *error == ModuleExportError::kResourceLimit
                           ? Error::kResourceLimit
                           : *error == ModuleExportError::kWrite
                                 ? Error::kWrite
                           : Error::kExport,
                 pc_);
    Account(std::get<std::size_t>(result));
    if (accumulator_.kind == Value::Kind::kClass)
      escaped_[accumulator_.ordinal] = 1;
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  const abc::MethodLookup& lookup_;
  const abc::MethodInfo& entry_;
  const abc::MethodBody& body_;
  const abc::ModuleInfo& module_;
  const analysis::MethodRoleIndex& roles_;
  ModuleExports exports_;
  std::ostream& output_;
  RecordProgramBudget budget_;
  std::vector<Value> registers_;
  std::vector<std::uint8_t> escaped_;
  std::vector<ClassState> classes_;
  std::vector<CreatedMethodState> created_methods_;
  std::vector<StaticAtom> atoms_;
  std::vector<RecordLiteralNode> nodes_;
  Value accumulator_{Value::Kind::kUnknown};
  abc::CodeOffset pc_{};
  std::size_t function_count_ = 0;
  std::size_t written_ = 0;
};
}  // namespace

RecordProgramResult WriteRecordProgram(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::RecordInfo& record, const abc::ModuleInfo& module,
    const analysis::MethodRoleIndex& roles, std::ostream& output,
    RecordProgramBudget budget) {
  if (header.kind != abc::AbcKind::kDynamic ||
      header.profile != abc::ProfileId::kDynamic1301)
    return Fail(Error::kProfile);
  if (record.first_method > index.methods.size() ||
      record.method_count > index.methods.size() - record.first_method)
    return Fail(Error::kEntry);
  const abc::MethodInfo* entry = nullptr;
  // v7 PandaFileTranslator::TranslateClasses maps this wire name to the
  // record entry. This is a validated boundary mapping, not string dispatch.
  constexpr std::string_view kEntryName = "func_main_0";
  for (const auto& method : std::span(index.methods).subspan(
           record.first_method, record.method_count)) {
    if (method.owner_offset != record.offset) return Fail(Error::kEntry);
    if (method.name != kEntryName) continue;
    if (entry) return Fail(Error::kEntry);
    entry = &method;
  }
  if (!entry || !entry->code) return Fail(Error::kEntry);
  try {
    const auto call = abc::ReadCallType(file, header, index,
                                        entry->annotations, {budget.bytes});
    if (const auto* error = std::get_if<abc::MetadataError>(&call))
      return Fail(Metadata(*error));
    const auto frame = abc::BuildFrameLayout(
        entry->code->registers, entry->code->arguments,
        abc::DecodeCallFlags(std::get<abc::CallType>(call).value));
    const auto* layout = std::get_if<abc::FrameLayout>(&frame);
    if (!layout || layout->parameters != 0) return Fail(Error::kFrame);
    const auto decoded = abc::DecodeMethodBody(
        file, header.profile, *entry->code, {budget.bytes});
    const auto* body = std::get_if<abc::MethodBody>(&decoded);
    if (!body) {
      const auto& error = std::get<abc::BodyFailure>(decoded);
      return Fail(error.kind == abc::BodyError::kResourceLimit
                      ? Error::kResourceLimit
                      : error.kind == abc::BodyError::kFile ? Error::kRead
                                                             : Error::kBody,
                  error.offset);
    }
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > budget.bytes / width) return false;
      budget.bytes -= count * width;
      return true;
    };
    if (!admit(body->instructions.capacity(), sizeof(abc::BodyInstruction)) ||
        !admit(body->tries.capacity(), sizeof(abc::TryRegion)) ||
        !admit(body->handlers.capacity(), sizeof(abc::CatchHandler)) ||
        !admit(layout->registers, sizeof(Value)) ||
        !admit(body->instructions.size(), sizeof(std::uint8_t)))
      return Fail(Error::kResourceLimit);
    const auto build_exports = [&]() {
      return BuildModuleExports(
          file, header, module,
          {budget.export_bytes, budget.strings});
    };
    auto dry_exports = build_exports();
    if (const auto* error = std::get_if<abc::MetadataError>(&dry_exports))
      return Fail(Metadata(*error));
    if (const auto* error = std::get_if<ModuleExportError>(&dry_exports))
      return Fail(*error == ModuleExportError::kResourceLimit
                      ? Error::kResourceLimit
                      : Error::kModule);
    DiscardBuffer discard;
    std::ostream dry(&discard);
    const auto checked = ProgramWriter(
        file, header, index, lookup, *entry, *layout, *body, module, roles,
        std::get<ModuleExports>(std::move(dry_exports)), dry, budget).Write();
    if (file.CheckUnchanged()) return Fail(Error::kFileChanged);
    auto actual_exports = build_exports();
    if (const auto* error = std::get_if<abc::MetadataError>(&actual_exports))
      return Fail(Metadata(*error));
    if (const auto* error = std::get_if<ModuleExportError>(&actual_exports))
      return Fail(*error == ModuleExportError::kResourceLimit
                      ? Error::kResourceLimit
                      : Error::kModule);
    const auto emitted = ProgramWriter(
        file, header, index, lookup, *entry, *layout, *body, module, roles,
        std::get<ModuleExports>(std::move(actual_exports)), output, budget)
                             .Write();
    if (checked != emitted || file.CheckUnchanged())
      return Fail(Error::kFileChanged);
    return emitted;
  } catch (const RecordProgramFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return Fail(Error::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(Error::kResourceLimit);
  } catch (const std::ios_base::failure&) {
    return Fail(Error::kWrite);
  }
}
}  // namespace abcd::source
