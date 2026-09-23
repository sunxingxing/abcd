#include "abcd/source/linear_body.h"

#include <array>
#include <charconv>
#include <limits>
#include <ostream>
#include <streambuf>

#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/ordinary_call.h"
#include "abcd/source/string_literal.h"
#include "linear_instruction.h"
#include "object_buffer.h"
#include "ordered_body.h"

namespace abcd::source {
namespace {
using namespace analysis;
enum class StringUse { kLiteral, kPropertyName };
bool IsElementName(std::u16string_view name) {
  constexpr auto kIndexDigits =
      std::numeric_limits<std::uint32_t>::digits10 + 1;
  constexpr std::uint64_t kMaxElementIndex = UINT32_MAX - 1;
  constexpr std::uint64_t kDecimalRadix = 10;
  if (name.empty() || name.size() > kIndexDigits ||
      (name.size() > 1 && name.front() == u'0'))
    return false;
  std::uint64_t value = 0;
  for (const auto digit : name) {
    if (digit < u'0' || digit > u'9') return false;
    value = value * kDecimalRadix + (digit - u'0');
  }
  return value <= kMaxElementIndex;
}
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};

class BodyWriter {
 public:
  BodyWriter(ScalarNames names, std::ostream& output, EmitBudget budget,
             const MethodConstants* constants,
             ObjectBufferBudget object_budget)
      : names_(names),
        output_(output),
        remaining_(budget.bytes),
        constants_(constants),
        object_budget_(object_budget) {}

  BodyEmitResult Write(const abc::MethodBody& body) {
    if (!body.tries.empty() || !body.handlers.empty())
      return BodyEmitFailure{BodyEmitError::kExceptionRegions, {}, {}, {}};
    return Sequence(body.instructions, false, true);
  }
  BodyEmitResult Instruction(const abc::BodyInstruction& instruction) {
    return Sequence(std::span(&instruction, 1), true, false);
  }
  BodyEmitResult Invocation(const NamedInvocation& invocation,
                            abc::CodeOffset offset) {
    offset_ = offset;
    try {
      NamedCall(invocation);
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
    }
  }

  BodyEmitResult Ordered(const abc::MethodBody& body, const NormalFlow& graph,
                         const AccumulatorFlow& flow) {
    try {
      // Each label surrounds the prefix ending immediately before its target.
      // Quotient order makes every non-self edge forward: breaking that label
      // executes its target next. A self edge repeats only its enclosing while.
      // Shared blocks are emitted once, without duplicating effectful code.
      const auto& order = flow.order.blocks;
      for (std::size_t i = order.size(); i > 1; --i) {
        Label(order[i - 1]);
        Text(": {\n");
      }
      for (std::size_t position = 0; position < order.size(); ++position) {
        const auto id = order[position];
        const auto& block = graph.blocks[id.value];
        if (position != 0) Text("}\n");
        bool repeats = false;
        for (std::size_t i = 0; i < block.successor_count; ++i)
          repeats |= block.successors[i].target == id;
        if (repeats) Text("while (true) {\n");
        const auto transfer = [&](BlockId target) {
          if (target == id)
            Text("continue;\n");
          else
            Break(target);
        };
        const bool branch = block.transfer == abc::ControlTransfer::kBranch;
        const bool jump = block.transfer == abc::ControlTransfer::kJump;
        const auto instructions =
            std::span(body.instructions)
                .subspan(block.first_instruction, block.instruction_count);
        const auto sequence =
            Sequence(instructions.first(instructions.size() - (branch || jump)),
                     flow.entries[id.value] == AccumulatorState::kValue, false);
        if (std::holds_alternative<BodyEmitFailure>(sequence)) return sequence;
        offset_ = instructions.back().offset;
        if (branch) {
          const auto condition =
              LiftBranchCondition(instructions.back().decoded);
          if (!std::holds_alternative<UnaryExpression>(condition))
            return Fail(BodyEmitError::kUnsupportedInstruction);
          for (std::size_t i = 0; i < block.successor_count; ++i)
            if (block.successors[i].kind == FlowEdgeKind::kTaken) {
              Text("if (");
              Expression(std::get<UnaryExpression>(condition));
              Text(") ");
              transfer(block.successors[i].target);
            }
        }
        for (std::size_t i = 0; i < block.successor_count; ++i)
          if (!branch || block.successors[i].kind == FlowEdgeKind::kFallthrough)
            transfer(block.successors[i].target);
        if (repeats) Text("}\n");
        if (error_) return *error_;
      }
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
    }
  }

 private:
  BodyEmitResult Sequence(std::span<const abc::BodyInstruction> instructions,
                          bool accumulator_defined, bool require_terminator) {
    bool terminated = false;
    try {
      for (std::size_t i = 0; i < instructions.size(); ++i) {
        const auto& instruction = instructions[i];
        offset_ = instruction.offset;
        if (terminated) return Fail(BodyEmitError::kTrailingInstructions);
        using abc::Opcode;
        const auto opcode = instruction.decoded.opcode;
        if (opcode == Opcode::kNOP) continue;
        if (opcode == Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16 ||
            opcode == Opcode::kCREATEOBJECTWITHBUFFER_IMM16_ID16) {
          if (!constants_) return Fail(BodyEmitError::kMissingConstants);
          const auto lifted = LiftObjectCreation(instruction.decoded);
          if (std::holds_alternative<ObjectCreationError>(lifted))
            return Fail(BodyEmitError::kUnsupportedInstruction);
          Expression(ScalarValue{Accumulator{}});
          Text(" = ");
          if (!error_) {
            const auto result = WriteObjectBuffer(
                std::get<ObjectCreation>(lifted), offset_, *constants_, output_,
                {remaining_}, object_budget_);
            if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
              return *failure;
            const auto count = std::get<std::size_t>(result);
            remaining_ -= count;
            written_ += count;
          }
          Text(";\n");
          accumulator_defined = true;
        } else if (opcode == Opcode::kLDA_STR_ID16) {
          StringLoad(instruction.decoded);
          accumulator_defined = true;
        } else if (opcode == Opcode::kLDOBJBYNAME_IMM8_ID16 ||
                   opcode == Opcode::kLDOBJBYNAME_IMM16_ID16) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          if (const auto invocation = MatchNamedInvocation(
                  instructions, i, names_.registers.size())) {
            NamedCall(*invocation);
            i += invocation->instruction_count - 1;
          } else {
            NamedLoad(instruction.decoded);
          }
        } else if (opcode == Opcode::kSTOBJBYNAME_IMM8_ID16_V8 ||
                   opcode == Opcode::kSTOBJBYNAME_IMM16_ID16_V8) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          NamedStore(instruction.decoded);
        } else if (opcode == Opcode::kCALLARG0_IMM8 ||
                   opcode == Opcode::kCALLARG1_IMM8_V8 ||
                   opcode == Opcode::kCALLARGS2_IMM8_V8_V8 ||
                   opcode == Opcode::kCALLARGS3_IMM8_V8_V8_V8 ||
                   opcode == Opcode::kCALLRANGE_IMM8_IMM8_V8) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          Call(instruction.decoded);
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
      if (error_) return *error_;
      if (require_terminator && !terminated)
        return Fail(BodyEmitError::kMissingTerminator);
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
    }
  }

  void Label(BlockId id) {
    constexpr std::size_t kLabelDigits = 10;
    std::array<char, kLabelDigits> buffer{};
    const auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), id.value);
    Text("next_");
    Text(std::string_view(buffer.data(), converted.ptr - buffer.data()));
  }
  void Break(BlockId target) {
    Text("break ");
    Label(target);
    Text(";\n");
  }
  void StringLoad(const abc::DecodedInstruction& instruction) {
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
    Expression(ScalarValue{Accumulator{}});
    Text(" = ");
    StringConstant(
        {static_cast<std::uint32_t>(instruction.operands[0].raw_bits)});
    Text(";\n");
  }
  void NamedLoad(const abc::DecodedInstruction& instruction) {
    const auto lifted = LiftNamedRead(instruction);
    if (std::holds_alternative<NamedReadError>(lifted)) {
      error_ = BodyEmitFailure{BodyEmitError::kUnsupportedInstruction,
                               offset_,
                               ScalarError::kMalformedInstruction,
                               {}};
      return;
    }
    Expression(ScalarValue{Accumulator{}});
    Text(" = (");
    Expression(ScalarValue{Accumulator{}});
    Text(")[");
    StringConstant(std::get<NamedRead>(lifted).name, StringUse::kPropertyName);
    Text("];\n");
  }
  void Call(const abc::DecodedInstruction& instruction) {
    const auto lifted = LiftOrdinaryCall(instruction, names_.registers.size());
    if (const auto* error = std::get_if<CallError>(&lifted)) {
      error_ = BodyEmitFailure{BodyEmitError::kUnsupportedInstruction,
                               offset_,
                               *error == CallError::kRegisterRange
                                   ? ScalarError::kRegisterRange
                                   : ScalarError::kMalformedInstruction,
                               {}};
      return;
    }
    const auto& call = std::get<OrdinaryCall>(lifted);
    Expression(ScalarValue{Accumulator{}});
    // Force a value call even if the supplied binding happens to be "eval".
    // No .call/.apply lookup, mutable intrinsic or spread iteration is allowed.
    Text(" = (0, ");
    Expression(ScalarValue{Accumulator{}});
    Text(")(");
    for (std::uint32_t i = 0; i < call.Count() && !error_; ++i) {
      if (i) Text(", ");
      Expression(ScalarValue{*call.At(i)});
    }
    Text(");\n");
  }
  void NamedCall(const NamedInvocation& invocation) {
    // The immediately preceding copy proves acc == explicit receiver. Native
    // Reference-call syntax preserves exactly one getter and the original this;
    // do not split the member expression or consult mutable call intrinsics.
    Expression(ScalarValue{Accumulator{}});
    Text(" = (");
    Expression(ScalarValue{Accumulator{}});
    Text(")[");
    StringConstant(invocation.property.name, StringUse::kPropertyName);
    Text("](");
    for (std::size_t i = 0; i < invocation.call.arguments.count && !error_;
         ++i) {
      if (i) Text(", ");
      Expression(ScalarValue{invocation.call.arguments.values[i]});
    }
    Text(");\n");
  }
  void NamedStore(const abc::DecodedInstruction& instruction) {
    const auto lifted = LiftNamedWrite(instruction, names_.registers.size());
    if (const auto* error = std::get_if<NamedWriteError>(&lifted)) {
      error_ = BodyEmitFailure{BodyEmitError::kUnsupportedInstruction,
                               offset_,
                               *error == NamedWriteError::kRegisterRange
                                   ? ScalarError::kRegisterRange
                                   : ScalarError::kMalformedInstruction,
                               {}};
      return;
    }
    const auto store = std::get<NamedWrite>(lifted);
    // RuntimeStObjByName requires throwing [[Set]] even in a sloppy caller.
    // Private parameters are fresh bindings; arguments are evaluated outside
    // their scope. Do not assign the setter/Proxy result back into acc.
    Text("(($object, $value) => { \"use strict\"; $object[");
    StringConstant(store.name, StringUse::kPropertyName);
    Text("] = $value; })(");
    Expression(ScalarValue{store.object});
    Text(", ");
    Expression(ScalarValue{Accumulator{}});
    Text(");\n");
  }
  void StringConstant(abc::PoolIndex slot,
                      StringUse use = StringUse::kLiteral) {
    if (error_) return;
    if (!constants_) {
      error_ = Fail(BodyEmitError::kMissingConstants);
      return;
    }
    const auto& context = *constants_;
    const auto reference = abc::ResolvePoolReference(
        context.file, context.header, context.index, context.method, slot,
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
    const auto& text = std::get<std::u16string>(string);
    // Pinned ObjectFastOperator's name path bypasses ordinary element storage.
    // Native JS [] would silently change indexed-name reads/writes on arrays.
    if (use == StringUse::kPropertyName && IsElementName(text)) {
      error_ = Fail(BodyEmitError::kIndexedName);
      return;
    }
    Account(WriteStringLiteral(text, output_, {remaining_}));
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
  ObjectBufferBudget object_budget_;
};
}  // namespace

BodyEmitResult WriteLinearInstruction(const abc::BodyInstruction& instruction,
                                      ScalarNames names, std::ostream& output,
                                      EmitBudget budget,
                                      const MethodConstants* constants,
                                      ObjectBufferBudget object_budget) {
  return BodyWriter(names, output, budget, constants, object_budget)
      .Instruction(instruction);
}

BodyEmitResult WriteNamedInvocation(const analysis::NamedInvocation& invocation,
                                    abc::CodeOffset offset, ScalarNames names,
                                    std::ostream& output, EmitBudget budget,
                                    const MethodConstants* constants) {
  return BodyWriter(names, output, budget, constants, {})
      .Invocation(invocation, offset);
}

BodyEmitResult WriteLinearBody(const abc::MethodBody& body, ScalarNames names,
                               std::ostream& output, EmitBudget budget,
                               const MethodConstants* constants,
                               ObjectBufferBudget object_budget) {
  // Validation is streaming too: do not retain a second copy of a large body
  // or leak a syntactically valid prefix when a late opcode cannot be lifted.
  DiscardBuffer buffer;
  std::ostream discard(&buffer);
  const auto checked =
      BodyWriter(names, discard, budget, constants, object_budget).Write(body);
  if (std::holds_alternative<BodyEmitFailure>(checked)) return checked;
  return BodyWriter(names, output, budget, constants, object_budget).Write(body);
}

BodyEmitResult WriteOrderedBody(const abc::MethodBody& body,
                                const NormalFlow& graph,
                                const AccumulatorFlow& flow, ScalarNames names,
                                std::ostream& output, EmitBudget budget,
                                const MethodConstants* constants,
                                ObjectBufferBudget object_budget) {
  DiscardBuffer buffer;
  std::ostream discard(&buffer);
  const auto checked =
      BodyWriter(names, discard, budget, constants, object_budget)
          .Ordered(body, graph, flow);
  if (std::holds_alternative<BodyEmitFailure>(checked)) return checked;
  return BodyWriter(names, output, budget, constants, object_budget)
      .Ordered(body, graph, flow);
}
}  // namespace abcd::source
