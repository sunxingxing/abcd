#include "abcd/source/static_fields.h"

#include <algorithm>
#include <bit>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>

#include "abcd/abc/call_type.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/abc/literal_array.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/define_instruction.h"
#include "abcd/analysis/scalar.h"

namespace abcd::source {
namespace {
using Error = StaticFieldError;
struct Ref {
  enum class Kind { kUnknown, kReceiver, kValue } kind = Kind::kUnknown;
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
              std::size_t bytes, abc::StringBudget strings)
      : file_(file), header_(header), index_(index), method_(method),
        body_(body), bytes_(bytes), strings_(strings),
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
    const auto& array = std::get<StaticArray>(value);
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
  StaticAtom Element(const abc::LiteralEntry& entry) {
    if (entry.tag == abc::LiteralTag::kString) {
      if (!std::holds_alternative<abc::EntityOffset>(entry.value))
        throw Fail(Error::kConstant, pc_);
      const auto text = abc::ReadStringEntity(
          file_, header_, std::get<abc::EntityOffset>(entry.value),
          {std::min(strings_.bytes, bytes_ - charged_)});
      if (const auto* error = std::get_if<abc::MetadataError>(&text))
        throw Fail(Metadata(*error), pc_);
      return std::get<std::u16string>(text);
    }
    const auto* bits = std::get_if<abc::LiteralBits>(&entry.value);
    if (!bits) throw Fail(Error::kConstant, pc_);
    switch (entry.tag) {
      case abc::LiteralTag::kNull:
        return StaticNull{};
      case abc::LiteralTag::kBoolean:
        if (bits->value > 1) throw Fail(Error::kConstant, pc_);
        return bits->value == 1;
      case abc::LiteralTag::kInteger:
        if (bits->value > UINT32_MAX) throw Fail(Error::kConstant, pc_);
        return std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(bits->value));
      case abc::LiteralTag::kFloat: {
        if (bits->value > UINT32_MAX) throw Fail(Error::kConstant, pc_);
        const auto value = std::bit_cast<float>(
            static_cast<std::uint32_t>(bits->value));
        return StaticFloat{std::bit_cast<std::uint64_t>(
            static_cast<double>(value))};
      }
      case abc::LiteralTag::kDouble:
        return StaticFloat{bits->value};
      default:
        throw Fail(Error::kConstant, pc_);
    }
  }
  void Array(const abc::DecodedInstruction& instruction) {
    const auto cache_max = instruction.opcode ==
                                   abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16
                               ? UINT8_MAX : UINT16_MAX;
    if (instruction.operand_count != 2 ||
        instruction.operands[0].kind != abc::OperandKind::kImmediate ||
        instruction.operands[0].raw_bits > cache_max ||
        instruction.operands[1].kind != abc::OperandKind::kLiteralArray ||
        instruction.operands[1].raw_bits > UINT16_MAX)
      throw Fail(Error::kInstruction, pc_);
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, method_.offset,
        {static_cast<std::uint32_t>(instruction.operands[1].raw_bits)},
        abc::PoolEntityKind::kLiteralArray);
    if (const auto* error = std::get_if<abc::MetadataError>(&reference))
      throw Fail(Metadata(*error), pc_);
    const auto decoded = abc::ReadLiteralArray(
        file_, header_, std::get<abc::PoolReference>(reference).offset,
        {bytes_ - charged_});
    if (const auto* error = std::get_if<abc::LiteralFailure>(&decoded))
      throw Fail(Literal(error->kind), pc_);
    const auto& entries = std::get<abc::LiteralArray>(decoded).entries;
    if (entries.capacity() > (bytes_ - charged_) /
                                 sizeof(abc::LiteralEntry))
      throw Fail(Error::kResourceLimit, pc_);
    const auto raw_bytes = entries.capacity() * sizeof(abc::LiteralEntry);
    Charge(raw_bytes, 1);
    if (entries.size() > (bytes_ - charged_) / sizeof(StaticAtom))
      throw Fail(Error::kResourceLimit, pc_);
    StaticArray array;
    array.elements.reserve(entries.size());
    if (array.elements.capacity() > (bytes_ - charged_) /
                                        sizeof(StaticAtom))
      throw Fail(Error::kResourceLimit, pc_);
    std::size_t temporary_bytes =
        array.elements.capacity() * sizeof(StaticAtom);
    Charge(temporary_bytes, 1);
    for (const auto& entry : entries) {
      array.elements.push_back(Element(entry));
      if (const auto* text =
              std::get_if<std::u16string>(&array.elements.back())) {
        Charge(text->capacity(), sizeof(char16_t));
        temporary_bytes = AddBytes(temporary_bytes, text->capacity(),
                                   sizeof(char16_t));
      }
    }
    charged_ -= temporary_bytes;
    accumulator_ = Save(std::move(array));
    charged_ -= raw_bytes;
  }
  void Define(const abc::DecodedInstruction& instruction) {
    const auto lifted = analysis::LiftOwnWrite(instruction, registers_.size());
    const auto* write = std::get_if<analysis::OwnWrite>(&lifted);
    if (!write || write->kind != analysis::OwnWriteKind::kDefineField ||
        registers_[write->object.value].kind != Ref::Kind::kReceiver ||
        accumulator_.kind != Ref::Kind::kValue)
      throw Fail(Error::kValue, pc_);
    AddField(ReadName(write->name));
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
    if (registers_[object.value].kind != Ref::Kind::kReceiver ||
        accumulator_.kind != Ref::Kind::kValue)
      throw Fail(Error::kValue, pc_);
    AddField(std::move(name));
  }
  void AddField(std::u16string name) {
    // Native static field syntax cannot define `prototype` on a constructor.
    if (name == u"prototype") throw Fail(Error::kValue, pc_);
    const auto array = std::holds_alternative<StaticArray>(
        fields_.values[accumulator_.value]);
    if (array && array_refs_[accumulator_.value] != kArrayUnseen)
      throw Fail(Error::kValue, pc_);
    Charge(name.capacity(), sizeof(char16_t));
    const auto old_capacity = fields_.fields.capacity();
    fields_.fields.push_back({std::move(name), accumulator_.value});
    Charge(fields_.fields.capacity() - old_capacity, sizeof(StaticField));
    if (array) array_refs_[accumulator_.value] = kArraySeen;
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
  std::vector<Ref> registers_;
  Ref accumulator_;
  StaticFields fields_;
  std::vector<std::uint8_t> array_refs_;
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
    } else {
      const auto& array = std::get<StaticArray>(value);
      bytes = AddBytes(bytes, array.elements.capacity(), sizeof(StaticAtom));
      for (const auto& element : array.elements)
        if (const auto* text = std::get_if<std::u16string>(&element))
          bytes = AddBytes(bytes, text->capacity(), sizeof(char16_t));
    }
  }
  for (const auto& field : fields)
    bytes = AddBytes(bytes, field.name.capacity(), sizeof(char16_t));
  return bytes;
}

StaticFieldResult RecoverStaticFields(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodInfo& method,
    std::size_t bytes, abc::StringBudget strings) {
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
                              bytes, strings).Read();
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
