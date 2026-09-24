#include "abcd/source/static_fields.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>

#include "abcd/abc/call_type.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/abc/literal_array.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/constructor_call.h"
#include "abcd/analysis/define_instruction.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/indexed_write.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/scalar.h"
#include "record_array.h"
#include "record_buffer.h"

namespace abcd::source {
namespace {
using Error = StaticFieldError;
struct Ref {
  enum class Kind { kUnknown, kReceiver, kValue, kImport } kind =
      Kind::kUnknown;
  std::size_t value = 0;
};
StaticFieldFailure Fail(Error kind, abc::CodeOffset pc = {}) {
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
      return Error::kConstant;
  }
}
Error Literal(abc::LiteralError error) {
  switch (error) {
    case abc::LiteralError::kResourceLimit:
      return Error::kResourceLimit;
    case abc::LiteralError::kRead:
      return Error::kRead;
    case abc::LiteralError::kFileChanged:
      return Error::kFileChanged;
    default:
      return Error::kConstant;
  }
}
std::size_t AddBytes(std::size_t current, std::size_t count,
                     std::size_t width) {
  return count > (SIZE_MAX - current) / width ? SIZE_MAX
                                               : current + count * width;
}
class FieldReader {
 public:
  FieldReader(const corpus::AbcFile& file, const abc::Header& header,
              const abc::MetadataIndex& index, const abc::MethodInfo& method,
              const abc::FrameLayout& frame, const abc::MethodBody& body,
              std::size_t bytes, abc::StringBudget strings,
              const abc::ModuleInfo* module)
      : file_(file), header_(header), index_(index), method_(method),
        body_(body), bytes_(bytes), strings_(strings), module_(module),
        registers_(frame.registers) {
    if (frame.this_value) registers_[frame.this_value->value].kind =
        Ref::Kind::kReceiver;
  }

  StaticFields Read() {
    if (!body_.tries.empty() || !body_.handlers.empty())
      throw Fail(Error::kInstruction);
    for (std::size_t i = 0; i < body_.instructions.size(); ++i) {
      const auto& site = body_.instructions[i];
      pc_ = site.offset;
      if (site.branch_target) throw Fail(Error::kInstruction, pc_);
      using abc::Opcode;
      switch (site.decoded.opcode) {
        case Opcode::kLDA_STR_ID16:
          String(site.decoded);
          break;
        case Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16:
        case Opcode::kCREATEARRAYWITHBUFFER_IMM16_ID16:
          Array(site.decoded);
          break;
        case Opcode::kCREATEEMPTYARRAY_IMM8:
        case Opcode::kCREATEEMPTYARRAY_IMM16:
          EmptyArray(site.decoded);
          break;
        case Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16:
        case Opcode::kCREATEOBJECTWITHBUFFER_IMM16_ID16:
          BufferedObject(site.decoded);
          break;
        case Opcode::kCREATEEMPTYOBJECT:
          EmptyObject(site.decoded);
          break;
        case Opcode::kLDEXTERNALMODULEVAR_IMM8:
        case Opcode::kWIDE_LDEXTERNALMODULEVAR_PREF_IMM16:
          LoadImport(site.decoded, i);
          break;
        case Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16:
          if (!pending_import_guard_ || *pending_import_guard_ != i)
            throw Fail(Error::kInstruction, pc_);
          pending_import_guard_.reset();
          break;
        case Opcode::kNEWOBJRANGE_IMM8_IMM8_V8:
        case Opcode::kNEWOBJRANGE_IMM16_IMM8_V8:
        case Opcode::kWIDE_NEWOBJRANGE_PREF_IMM16_V8:
          Construct(site.decoded);
          break;
        case Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16:
        case Opcode::kSTOWNBYINDEX_IMM16_V8_IMM16:
        case Opcode::kWIDE_STOWNBYINDEX_PREF_V8_IMM32:
          StoreArrayElement(site.decoded);
          break;
        case Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8:
        case Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8:
          Define(site.decoded);
          break;
        case Opcode::kCALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8:
        case Opcode::kCALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8:
          DefineComputed(site.decoded);
          break;
        case Opcode::kRETURNUNDEFINED:
          if (i + 1 != body_.instructions.size() ||
              site.decoded.operand_count != 0)
            throw Fail(Error::kInstruction, pc_);
          return std::move(fields_);
        case Opcode::kNOP:
          break;
        default:
          Scalar(site.decoded);
          break;
      }
    }
    throw Fail(Error::kInstruction, pc_);
  }

 private:
  void Charge(std::size_t count, std::size_t width) {
    if (count > (bytes_ - charged_) / width)
      throw Fail(Error::kResourceLimit, pc_);
    charged_ += count * width;
  }
  void AccountValue(const StaticValue& value) {
    if (const auto* atom = std::get_if<StaticAtom>(&value)) {
      if (const auto* text = std::get_if<std::u16string>(atom))
        Charge(text->capacity(), sizeof(char16_t));
      return;
    }
    const auto* array_value = std::get_if<StaticArray>(&value);
    if (!array_value) return;
    const auto& array = *array_value;
    Charge(array.elements.capacity(), sizeof(StaticAtom));
    for (const auto& atom : array.elements)
      if (const auto* text = std::get_if<std::u16string>(&atom))
        Charge(text->capacity(), sizeof(char16_t));
  }
  Ref Save(StaticValue value) {
    if (fields_.values.size() >= body_.instructions.size())
      throw Fail(Error::kResourceLimit, pc_);
    AccountValue(value);
    const auto index = fields_.values.size();
    const auto old_value_capacity = fields_.values.capacity();
    fields_.values.push_back(std::move(value));
    Charge(fields_.values.capacity() - old_value_capacity,
           sizeof(StaticValue));
    const auto old_alias_capacity = array_refs_.capacity();
    array_refs_.push_back(kArrayUnseen);
    Charge(array_refs_.capacity() - old_alias_capacity, sizeof(std::uint8_t));
    return {Ref::Kind::kValue, index};
  }
  std::u16string ReadName(abc::PoolIndex index) {
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, method_.offset, index,
        abc::PoolEntityKind::kString);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      throw Fail(Metadata(*error), pc_);
    const auto text = abc::ReadStringEntity(
        file_, header_, std::get<abc::PoolReference>(reference).offset,
        {std::min(strings_.bytes, bytes_ - charged_)});
    if (const auto* error = std::get_if<abc::MetadataError>(&text))
      throw Fail(Metadata(*error), pc_);
    return std::get<std::u16string>(text);
  }
  void String(const abc::DecodedInstruction& instruction) {
    if (instruction.operand_count != 1 ||
        instruction.operands[0].kind != abc::OperandKind::kString ||
        instruction.operands[0].raw_bits > UINT16_MAX)
      throw Fail(Error::kInstruction, pc_);
    accumulator_ = Save(StaticAtom{ReadName({static_cast<std::uint32_t>(
        instruction.operands[0].raw_bits)})});
  }
  void Array(const abc::DecodedInstruction& instruction) {
    auto decoded = ReadBufferedArray(
        file_, header_, index_, method_.offset, instruction,
        bytes_ - charged_, strings_);
    if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
      throw Fail(Metadata(*error), pc_);
    if (const auto* error = std::get_if<abc::LiteralFailure>(&decoded))
      throw Fail(Literal(error->kind), pc_);
    if (const auto* error = std::get_if<BufferedArrayError>(&decoded))
      throw Fail(*error == BufferedArrayError::kResourceLimit
                     ? Error::kResourceLimit
                     : *error == BufferedArrayError::kInstruction
                           ? Error::kInstruction : Error::kConstant,
                 pc_);
    accumulator_ = Save(StaticArray{
        std::move(std::get<BufferedArray>(decoded).elements)});
  }
  void EmptyArray(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftScalarInstruction(
        instruction, registers_.size());
    const auto* assignment = std::get_if<analysis::ScalarAssignment>(&lifted);
    const auto* value = assignment
                            ? std::get_if<analysis::ScalarValue>(
                                  &assignment->expression)
                            : nullptr;
    if (!value || !std::holds_alternative<analysis::EmptyArray>(*value) ||
        !std::holds_alternative<analysis::Accumulator>(assignment->destination))
      throw Fail(Error::kInstruction, pc_);
    NewNode(RecordLiteralKind::kArray);
  }
  void NewNode(RecordLiteralKind kind) {
    if (fields_.nodes.size() >= body_.instructions.size())
      throw Fail(Error::kResourceLimit, pc_);
    const auto id = fields_.nodes.size();
    const auto capacity = fields_.nodes.capacity();
    fields_.nodes.push_back(RecordLiteralNode{kind});
    Charge(fields_.nodes.capacity() - capacity, sizeof(RecordLiteralNode));
    accumulator_ = Save(RecordNodeRef{id});
  }
  void EmptyObject(const abc::DecodedInstruction& instruction) {
    if (!std::holds_alternative<analysis::EmptyObjectCreation>(
            analysis::LiftObjectCreation(instruction)))
      throw Fail(Error::kInstruction, pc_);
    NewNode(RecordLiteralKind::kObject);
  }
  void BufferedObject(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftObjectCreation(instruction);
    const auto* creation =
        std::get_if<analysis::BufferedObjectCreation>(&lifted);
    if (!creation) throw Fail(Error::kInstruction, pc_);
    auto decoded = ReadRecordObjectBuffer(
        file_, header_, index_, method_.offset, creation->literal,
        bytes_ - charged_, strings_);
    if (const auto* error = std::get_if<abc::MetadataError>(&decoded))
      throw Fail(Metadata(*error), pc_);
    if (const auto* error = std::get_if<abc::LiteralFailure>(&decoded))
      throw Fail(Literal(error->kind), pc_);
    if (const auto* error = std::get_if<RecordBufferError>(&decoded))
      throw Fail(*error == RecordBufferError::kResourceLimit
                     ? Error::kResourceLimit : Error::kConstant, pc_);
    auto buffer = std::get<BufferedRecordObject>(std::move(decoded));
    Charge(buffer.retained_bytes, 1);
    NewNode(RecordLiteralKind::kObject);
    fields_.nodes[std::get<RecordNodeRef>(
        fields_.values[accumulator_.value]).id].fields =
        std::move(buffer.fields);
  }
  void LoadImport(const abc::DecodedInstruction& instruction,
                  std::size_t position) {
    const auto lifted = analysis::LiftModuleLoad(instruction);
    const auto* load = std::get_if<analysis::ModuleLoad>(&lifted);
    if (!module_ || !load ||
        load->space != analysis::ModuleSpace::kExternal ||
        position + 1 >= body_.instructions.size())
      throw Fail(Error::kInstruction, pc_);
    const auto guard = analysis::LiftHoleInstruction(
        body_.instructions[position + 1].decoded, registers_.size());
    const auto* checked = std::get_if<analysis::HoleGuard>(&guard);
    if (!checked ||
        !std::holds_alternative<analysis::Accumulator>(checked->value) ||
        !std::holds_alternative<analysis::HoleNameSlot>(checked->name))
      throw Fail(Error::kInstruction, pc_);
    const auto slot = static_cast<std::size_t>(load->slot.value);
    if (slot >= module_->imports.size() +
                    module_->namespace_imports.size())
      throw Fail(Error::kValue, pc_);
    const auto request = slot < module_->imports.size()
                             ? module_->imports[slot].request
                             : module_->namespace_imports[
                                   slot - module_->imports.size()].request;
    if (request.value >= module_->requests.size())
      throw Fail(Error::kValue, pc_);
    accumulator_ = {Ref::Kind::kImport, slot};
    pending_import_guard_ = position + 1;
  }
  void Construct(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftConstructorCall(
        instruction, registers_.size());
    const auto* call = std::get_if<analysis::ConstructorCall>(&lifted);
    if (!call) throw Fail(Error::kInstruction, pc_);
    const auto constructor = registers_[call->inputs.first.value];
    if (constructor.kind != Ref::Kind::kImport ||
        constructor.value > UINT16_MAX)
      throw Fail(Error::kValue, pc_);
    NewNode(RecordLiteralKind::kImportedConstruct);
    const auto root = std::get<RecordNodeRef>(
        fields_.values[accumulator_.value]).id;
    fields_.nodes[root].imported_slot = {
        static_cast<std::uint16_t>(constructor.value)};
    for (std::uint32_t i = 0; i < call->ArgumentCount(); ++i) {
      const auto argument = call->ArgumentAt(i);
      if (!argument) throw Fail(Error::kInstruction, pc_);
      const auto previous = accumulator_;
      accumulator_ = registers_[argument->value];
      auto value = NodeValue(root, true);
      accumulator_ = previous;
      if (const auto* atom = std::get_if<StaticAtom>(&value))
        if (const auto* text = std::get_if<std::u16string>(atom))
          Charge(text->capacity(), sizeof(char16_t));
      auto& node = fields_.nodes[root];
      const auto old_capacity = node.elements.capacity();
      node.elements.push_back(std::move(value));
      Charge(node.elements.capacity() - old_capacity,
             sizeof(RecordLiteralValue));
      if (const auto* child =
              std::get_if<RecordNodeRef>(&node.elements.back()))
        fields_.nodes[child->id].attached = true;
    }
  }
  RecordLiteralValue NodeValue(std::size_t parent,
                               bool earlier_child = false) {
    if (accumulator_.kind != Ref::Kind::kValue)
      throw Fail(Error::kValue, pc_);
    auto& value = fields_.values[accumulator_.value];
    if (const auto* atom = std::get_if<StaticAtom>(&value)) return *atom;
    if (auto* array = std::get_if<StaticArray>(&value)) {
      // Transfer an unpublished buffer array into the single-parent graph.
      // Replacing its value slot makes every later alias observe attachment.
      if (array_refs_[accumulator_.value] != kArrayUnseen ||
          fields_.nodes.size() >= body_.instructions.size())
        throw Fail(Error::kValue, pc_);
      RecordLiteralNode promoted{RecordLiteralKind::kArray};
      if (array->elements.size() >
          (bytes_ - charged_) / sizeof(RecordLiteralValue))
        throw Fail(Error::kResourceLimit, pc_);
      promoted.elements.reserve(array->elements.size());
      Charge(promoted.elements.capacity(), sizeof(RecordLiteralValue));
      for (auto& element : array->elements)
        promoted.elements.push_back(std::move(element));
      const auto released = array->elements.capacity() * sizeof(StaticAtom);
      const auto id = fields_.nodes.size();
      const auto capacity = fields_.nodes.capacity();
      fields_.nodes.push_back(std::move(promoted));
      Charge(fields_.nodes.capacity() - capacity,
             sizeof(RecordLiteralNode));
      value = RecordNodeRef{id};
      charged_ -= released;
      return RecordNodeRef{id};
    }
    const auto* child = std::get_if<RecordNodeRef>(&value);
    if (!child || child->id == parent ||
        (!earlier_child && child->id < parent) ||
        child->id >= fields_.nodes.size() ||
        fields_.nodes[child->id].attached ||
        fields_.nodes[child->id].emitted)
      throw Fail(Error::kValue, pc_);
    return *child;
  }
  void AddObjectField(abc::RegisterId object, std::u16string name) {
    const auto target = registers_[object.value];
    if (target.kind != Ref::Kind::kValue) throw Fail(Error::kValue, pc_);
    const auto* ref = std::get_if<RecordNodeRef>(
        &fields_.values[target.value]);
    if (!ref || ref->id >= fields_.nodes.size())
      throw Fail(Error::kValue, pc_);
    if (fields_.nodes[ref->id].kind != RecordLiteralKind::kObject ||
        fields_.nodes[ref->id].attached || fields_.nodes[ref->id].emitted)
      throw Fail(Error::kValue, pc_);
    auto value = NodeValue(ref->id);
    auto& node = fields_.nodes[ref->id];
    Charge(name.capacity(), sizeof(char16_t));
    if (const auto* atom = std::get_if<StaticAtom>(&value))
      if (const auto* text = std::get_if<std::u16string>(atom))
        Charge(text->capacity(), sizeof(char16_t));
    const auto old_capacity = node.fields.capacity();
    node.fields.push_back({std::move(name), std::move(value)});
    Charge(node.fields.capacity() - old_capacity,
           sizeof(RecordLiteralField));
    if (const auto* child =
            std::get_if<RecordNodeRef>(&node.fields.back().value))
      fields_.nodes[child->id].attached = true;
  }
  void StoreArrayElement(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftIndexedWrite(
        instruction, registers_.size());
    const auto* write = std::get_if<analysis::IndexedWrite>(&lifted);
    if (!write) throw Fail(Error::kInstruction, pc_);
    const auto target = registers_[write->object.value];
    if (target.kind != Ref::Kind::kValue)
      throw Fail(Error::kValue, pc_);
    if (const auto* ref = std::get_if<RecordNodeRef>(
            &fields_.values[target.value])) {
      if (ref->id >= fields_.nodes.size())
        throw Fail(Error::kValue, pc_);
      if (fields_.nodes[ref->id].kind != RecordLiteralKind::kArray ||
          fields_.nodes[ref->id].attached ||
          fields_.nodes[ref->id].emitted ||
          write->index != fields_.nodes[ref->id].elements.size())
        throw Fail(Error::kValue, pc_);
      auto value = NodeValue(ref->id);
      auto& node = fields_.nodes[ref->id];
      const auto old_capacity = node.elements.capacity();
      if (const auto* atom = std::get_if<StaticAtom>(&value))
        if (const auto* text = std::get_if<std::u16string>(atom))
          Charge(text->capacity(), sizeof(char16_t));
      node.elements.push_back(std::move(value));
      Charge(node.elements.capacity() - old_capacity,
             sizeof(RecordLiteralValue));
      if (const auto* child =
              std::get_if<RecordNodeRef>(&node.elements.back()))
        fields_.nodes[child->id].attached = true;
      return;
    }
    if (!std::holds_alternative<StaticArray>(fields_.values[target.value]) ||
        array_refs_[target.value] != kArrayUnseen ||
        accumulator_.kind != Ref::Kind::kValue ||
        !std::holds_alternative<StaticAtom>(
            fields_.values[accumulator_.value]))
      throw Fail(Error::kValue, pc_);
    auto& array = std::get<StaticArray>(fields_.values[target.value]);
    if (write->index != array.elements.size() ||
        array.elements.size() >= body_.instructions.size())
      throw Fail(Error::kValue, pc_);
    const auto& atom = std::get<StaticAtom>(
        fields_.values[accumulator_.value]);
    const auto copied_text = std::get_if<std::u16string>(&atom);
    if (copied_text) Charge(copied_text->size(), sizeof(char16_t));
    const auto old_capacity = array.elements.capacity();
    array.elements.push_back(atom);
    Charge(array.elements.capacity() - old_capacity, sizeof(StaticAtom));
    if (copied_text) {
      const auto& stored = std::get<std::u16string>(array.elements.back());
      Charge(stored.capacity() - copied_text->size(), sizeof(char16_t));
    }
  }
  void Define(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftOwnWrite(instruction, registers_.size());
    const auto* write = std::get_if<analysis::OwnWrite>(&lifted);
    if (!write || write->kind != analysis::OwnWriteKind::kDefineField)
      throw Fail(Error::kValue, pc_);
    auto name = ReadName(write->name);
    if (registers_[write->object.value].kind == Ref::Kind::kReceiver) {
      if (accumulator_.kind != Ref::Kind::kValue)
        throw Fail(Error::kValue, pc_);
      AddField(std::move(name));
    } else {
      AddObjectField(write->object, std::move(name));
    }
  }
  static std::u16string Decimal(std::uint32_t number) {
    std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 2> chars{};
    const auto result = std::to_chars(chars.data(), chars.data() + chars.size(),
                                      number);
    std::u16string name;
    for (const char* pos = chars.data(); pos != result.ptr; ++pos)
      name.push_back(static_cast<char16_t>(*pos));
    return name;
  }
  void DefineComputed(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftDefineInstruction(
        instruction, registers_.size());
    std::u16string name;
    abc::RegisterId object;
    if (const auto* indexed =
            std::get_if<analysis::DefineFieldByIndex>(&lifted)) {
      object = indexed->object;
      name = Decimal(indexed->index);
    } else if (const auto* computed =
                   std::get_if<analysis::DefineFieldByValue>(&lifted)) {
      object = computed->object;
      const auto& key = registers_[computed->key.value];
      if (key.kind != Ref::Kind::kValue ||
          !std::holds_alternative<StaticAtom>(fields_.values[key.value]))
        throw Fail(Error::kValue, pc_);
      const auto& atom = std::get<StaticAtom>(fields_.values[key.value]);
      if (const auto* text = std::get_if<std::u16string>(&atom))
        name = *text;
      else if (const auto* integer = std::get_if<std::int32_t>(&atom)) {
        if (*integer < 0) throw Fail(Error::kValue, pc_);
        name = Decimal(static_cast<std::uint32_t>(*integer));
      } else {
        throw Fail(Error::kValue, pc_);
      }
    } else {
      throw Fail(Error::kInstruction, pc_);
    }
    if (registers_[object.value].kind == Ref::Kind::kReceiver) {
      if (accumulator_.kind != Ref::Kind::kValue)
        throw Fail(Error::kValue, pc_);
      AddField(std::move(name));
    } else {
      AddObjectField(object, std::move(name));
    }
  }
  void AddField(std::u16string name) {
    // Native static field syntax cannot define `prototype` on a constructor.
    if (name == u"prototype") throw Fail(Error::kValue, pc_);
    const auto& value = fields_.values[accumulator_.value];
    const auto array = std::holds_alternative<StaticArray>(value);
    if (array && array_refs_[accumulator_.value] != kArrayUnseen)
      throw Fail(Error::kValue, pc_);
    const auto* node = std::get_if<RecordNodeRef>(&value);
    if (node && (node->id >= fields_.nodes.size() ||
                 fields_.nodes[node->id].attached ||
                 fields_.nodes[node->id].emitted))
      throw Fail(Error::kValue, pc_);
    Charge(name.capacity(), sizeof(char16_t));
    const auto old_capacity = fields_.fields.capacity();
    fields_.fields.push_back({std::move(name), accumulator_.value});
    Charge(fields_.fields.capacity() - old_capacity, sizeof(StaticField));
    if (array) array_refs_[accumulator_.value] = kArraySeen;
    if (node) fields_.nodes[node->id].emitted = true;
  }
  void Scalar(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftScalarInstruction(
        instruction, registers_.size());
    const auto* assignment = std::get_if<analysis::ScalarAssignment>(&lifted);
    if (!assignment || assignment->completion !=
                           analysis::CompletionEffect::kNormal)
      throw Fail(Error::kInstruction, pc_);
    const auto* source =
        std::get_if<analysis::ScalarValue>(&assignment->expression);
    if (!source) throw Fail(Error::kInstruction, pc_);
    Ref value;
    if (std::holds_alternative<analysis::Accumulator>(*source))
      value = accumulator_;
    else if (const auto* register_id =
                 std::get_if<abc::RegisterId>(source))
      value = registers_[register_id->value];
    else if (const auto* integer =
                 std::get_if<analysis::IntegerLiteral>(source))
      value = Save(StaticAtom{integer->value});
    else if (const auto* number =
                 std::get_if<analysis::Float64Bits>(source))
      value = Save(StaticAtom{StaticFloat{number->value}});
    else if (const auto* special =
                 std::get_if<analysis::SpecialValue>(source)) {
      switch (*special) {
        case analysis::SpecialValue::kUndefined:
          value = Save(StaticAtom{StaticUndefined{}});
          break;
        case analysis::SpecialValue::kNull:
          value = Save(StaticAtom{StaticNull{}});
          break;
        case analysis::SpecialValue::kTrue:
          value = Save(StaticAtom{true});
          break;
        case analysis::SpecialValue::kFalse:
          value = Save(StaticAtom{false});
          break;
        default:
          throw Fail(Error::kInstruction, pc_);
      }
    } else {
      throw Fail(Error::kInstruction, pc_);
    }
    if (std::holds_alternative<analysis::Accumulator>(assignment->destination))
      accumulator_ = value;
    else
      registers_[std::get<abc::RegisterId>(
          assignment->destination).value] = value;
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  const abc::MethodInfo& method_;
  const abc::MethodBody& body_;
  std::size_t bytes_;
  abc::StringBudget strings_;
  const abc::ModuleInfo* module_;
  std::vector<Ref> registers_;
  Ref accumulator_;
  StaticFields fields_;
  std::vector<std::uint8_t> array_refs_;
  std::optional<std::size_t> pending_import_guard_;
  static constexpr std::uint8_t kArrayUnseen = 0;
  static constexpr std::uint8_t kArraySeen = 1;
  std::size_t charged_ = 0;
  abc::CodeOffset pc_{};
};
}  // namespace

std::size_t StaticFields::AllocatedBytes() const {
  std::size_t bytes = AddBytes(0, values.capacity(), sizeof(StaticValue));
  bytes = AddBytes(bytes, fields.capacity(), sizeof(StaticField));
  for (const auto& value : values) {
    if (const auto* atom = std::get_if<StaticAtom>(&value)) {
      if (const auto* text = std::get_if<std::u16string>(atom))
        bytes = AddBytes(bytes, text->capacity(), sizeof(char16_t));
    } else if (const auto* array_value = std::get_if<StaticArray>(&value)) {
      const auto& array = *array_value;
      bytes = AddBytes(bytes, array.elements.capacity(), sizeof(StaticAtom));
      for (const auto& element : array.elements)
        if (const auto* text = std::get_if<std::u16string>(&element))
          bytes = AddBytes(bytes, text->capacity(), sizeof(char16_t));
    }
  }
  for (const auto& field : fields)
    bytes = AddBytes(bytes, field.name.capacity(), sizeof(char16_t));
  bytes = AddBytes(bytes, nodes.capacity(), sizeof(RecordLiteralNode));
  for (const auto& node : nodes) {
    bytes = AddBytes(bytes, node.fields.capacity(),
                     sizeof(RecordLiteralField));
    bytes = AddBytes(bytes, node.elements.capacity(),
                     sizeof(RecordLiteralValue));
    for (const auto& field : node.fields) {
      bytes = AddBytes(bytes, field.key.capacity(), sizeof(char16_t));
      if (const auto* atom = std::get_if<StaticAtom>(&field.value))
        if (const auto* text = std::get_if<std::u16string>(atom))
          bytes = AddBytes(bytes, text->capacity(), sizeof(char16_t));
    }
    for (const auto& element : node.elements)
      if (const auto* atom = std::get_if<StaticAtom>(&element))
        if (const auto* text = std::get_if<std::u16string>(atom))
          bytes = AddBytes(bytes, text->capacity(), sizeof(char16_t));
  }
  return bytes;
}

StaticFieldResult RecoverStaticFields(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodInfo& method,
    std::size_t bytes, abc::StringBudget strings,
    const abc::ModuleInfo* module) {
  // Both decoded profiles use DefineField for these admitted constant writes;
  // keep other historical versions behind a separate semantic proof.
  const bool proven_profile =
      header.profile == abc::ProfileId::kDynamic1206 ||
      header.profile == abc::ProfileId::kDynamic1301;
  if (!proven_profile || !method.code)
    return Fail(Error::kCode);
  try {
    const auto call = abc::ReadCallType(file, header, index,
                                        method.annotations, {bytes});
    if (const auto* error = std::get_if<abc::MetadataError>(&call))
      return Fail(Metadata(*error));
    const auto frame = abc::BuildFrameLayout(
        method.code->registers, method.code->arguments,
        abc::DecodeCallFlags(std::get<abc::CallType>(call).value));
    const auto* layout = std::get_if<abc::FrameLayout>(&frame);
    if (!layout || layout->parameters != 0 || !layout->this_value)
      return Fail(Error::kFrame);
    const auto decoded = abc::DecodeMethodBody(
        file, header.profile, *method.code, {bytes});
    const auto* body = std::get_if<abc::MethodBody>(&decoded);
    if (!body) {
      const auto& failure = std::get<abc::BodyFailure>(decoded);
      return Fail(failure.kind == abc::BodyError::kResourceLimit
                      ? Error::kResourceLimit
                      : failure.kind == abc::BodyError::kFile ? Error::kRead
                                                               : Error::kCode,
                  failure.offset);
    }
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > bytes / width) return false;
      bytes -= count * width;
      return true;
    };
    if (!admit(body->instructions.capacity(), sizeof(abc::BodyInstruction)) ||
        !admit(body->tries.capacity(), sizeof(abc::TryRegion)) ||
        !admit(body->handlers.capacity(), sizeof(abc::CatchHandler)) ||
        !admit(layout->registers, sizeof(Ref)))
      return Fail(Error::kResourceLimit);
    auto fields = FieldReader(file, header, index, method, *layout, *body,
                              bytes, strings, module).Read();
    if (file.CheckUnchanged()) return Fail(Error::kFileChanged);
    return fields;
  } catch (const StaticFieldFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return Fail(Error::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(Error::kResourceLimit);
  }
}
}  // namespace abcd::source
