#include "abcd/source/linear_body.h"

#include <ostream>
#include <streambuf>
#include <type_traits>

#include "abcd/abc/pool_reference.h"
#include "abcd/source/string_literal.h"

namespace abcd::source {
namespace {
using namespace analysis;
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};

bool ReadsAccumulator(const ScalarExpression& expression) {
  const auto reads = [](const ScalarValue& value) {
    return std::holds_alternative<Accumulator>(value);
  };
  return std::visit(
      [&](const auto& node) {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, ScalarValue>) {
          return reads(node);
        } else if constexpr (std::is_same_v<Node, UnaryExpression>) {
          return reads(node.input);
        } else {
          return reads(node.left) || reads(node.right);
        }
      },
      expression);
}

class BodyWriter {
 public:
  BodyWriter(ScalarNames names, std::ostream& output, EmitBudget budget,
             const MethodConstants* constants)
      : names_(names),
        output_(output),
        remaining_(budget.bytes),
        constants_(constants) {}

  BodyEmitResult Write(const abc::MethodBody& body) {
    if (!body.tries.empty() || !body.handlers.empty())
      return BodyEmitFailure{BodyEmitError::kExceptionRegions, {}, {}, {}};
    bool accumulator_defined = false;
    bool terminated = false;
    try {
      for (const auto& instruction : body.instructions) {
        offset_ = instruction.offset;
        if (terminated) return Fail(BodyEmitError::kTrailingInstructions);
        using abc::Opcode;
        const auto opcode = instruction.decoded.opcode;
        if (opcode == Opcode::kNOP) continue;
        if (opcode == Opcode::kLDA_STR_ID16) {
          StringLoad(instruction.decoded);
          accumulator_defined = true;
        } else if (opcode == Opcode::kRETURNUNDEFINED) {
          Text("return;\n");
          terminated = true;
        } else if (opcode == Opcode::kRETURN ||
                   opcode == Opcode::kTHROW_PREF_NONE) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          Text(opcode == Opcode::kRETURN ? "return " : "throw ");
          Expression(ScalarValue{Accumulator{}});
          Text(";\n");
          terminated = true;
        } else {
          const auto lifted = LiftScalarInstruction(instruction.decoded,
                                                    names_.registers.size());
          if (const auto* error = std::get_if<ScalarError>(&lifted))
            return BodyEmitFailure{
                BodyEmitError::kUnsupportedInstruction, offset_, *error, {}};
          const auto& assignment = std::get<ScalarAssignment>(lifted);
          if (!accumulator_defined && ReadsAccumulator(assignment.expression))
            return Fail(BodyEmitError::kAccumulatorHole);
          std::visit([&](const auto& slot) { Expression(ScalarValue{slot}); },
                     assignment.destination);
          Text(" = ");
          Expression(assignment.expression);
          Text(";\n");
          if (std::holds_alternative<Accumulator>(assignment.destination))
            accumulator_defined = true;
        }
        if (error_) return *error_;
      }
      if (!terminated) return Fail(BodyEmitError::kMissingTerminator);
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
    }
  }

 private:
  void StringLoad(const abc::DecodedInstruction& instruction) {
    if (!constants_) {
      error_ = Fail(BodyEmitError::kMissingConstants);
      return;
    }
    constexpr std::uint64_t kStringSlotMax = UINT16_MAX;
    if (instruction.operand_count != 1 ||
        instruction.operands[0].kind != abc::OperandKind::kString ||
        instruction.operands[0].raw_bits > kStringSlotMax) {
      error_ = BodyEmitFailure{BodyEmitError::kUnsupportedInstruction,
                               offset_,
                               ScalarError::kMalformedInstruction,
                               {}};
      return;
    }
    const auto& context = *constants_;
    const auto reference = abc::ResolvePoolReference(
        context.file, context.header, context.index, context.method,
        {static_cast<std::uint32_t>(instruction.operands[0].raw_bits)},
        abc::PoolEntityKind::kString);
    if (const auto* failure = std::get_if<abc::MetadataError>(&reference)) {
      error_ =
          BodyEmitFailure{BodyEmitError::kConstant, offset_, {}, {}, *failure};
      return;
    }
    const auto string = abc::ReadStringEntity(
        context.file, context.header,
        std::get<abc::PoolReference>(reference).offset, context.string_budget);
    if (const auto* failure = std::get_if<abc::MetadataError>(&string)) {
      error_ =
          BodyEmitFailure{BodyEmitError::kConstant, offset_, {}, {}, *failure};
      return;
    }
    Expression(ScalarValue{Accumulator{}});
    Text(" = ");
    if (error_) return;
    Account(WriteStringLiteral(std::get<std::u16string>(string), output_,
                               {remaining_}));
    Text(";\n");
  }
  BodyEmitFailure Fail(BodyEmitError kind) const {
    return {kind, offset_, {}, {}};
  }
  void Text(std::string_view text) {
    if (error_) return;
    if (text.size() > remaining_) {
      error_ = BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kOutputLimit};
      return;
    }
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) {
      error_ = BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
      return;
    }
    remaining_ -= text.size();
    written_ += text.size();
  }
  void Expression(const ScalarExpression& expression) {
    if (error_) return;
    const auto result =
        WriteScalarExpression(expression, names_, output_, {remaining_});
    Account(result);
  }
  void Account(const std::variant<std::size_t, EmitError>& result) {
    if (const auto* failure = std::get_if<EmitError>(&result)) {
      error_ =
          BodyEmitFailure{BodyEmitError::kExpression, offset_, {}, *failure};
    } else {
      const auto count = std::get<std::size_t>(result);
      remaining_ -= count;
      written_ += count;
    }
  }
  ScalarNames names_;
  std::ostream& output_;
  std::size_t remaining_;
  std::size_t written_ = 0;
  abc::CodeOffset offset_;
  std::optional<BodyEmitFailure> error_;
  const MethodConstants* constants_;
};
}  // namespace

BodyEmitResult WriteLinearBody(const abc::MethodBody& body, ScalarNames names,
                               std::ostream& output, EmitBudget budget,
                               const MethodConstants* constants) {
  // Validation is streaming too: do not retain a second copy of a large body
  // or leak a syntactically valid prefix when a late opcode cannot be lifted.
  DiscardBuffer buffer;
  std::ostream discard(&buffer);
  const auto checked =
      BodyWriter(names, discard, budget, constants).Write(body);
  if (std::holds_alternative<BodyEmitFailure>(checked)) return checked;
  return BodyWriter(names, output, budget, constants).Write(body);
}
}  // namespace abcd::source
