#include "abcd/source/linear_body.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <ostream>
#include <streambuf>

#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/constant_object.h"
#include "abcd/analysis/constructor_call.h"
#include "abcd/analysis/derived_constructor.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/iter_result.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/ordinary_call.h"
#include "abcd/analysis/rest_arguments.h"
#include "abcd/analysis/value_write.h"
#include "abcd/source/string_literal.h"
#include "branch_source_facts.h"
#include "capture_stream.h"
#include "element_name.h"
#include "linear_instruction.h"
#include "object_buffer.h"
#include "ordered_body.h"

namespace abcd::source {
namespace {
using namespace analysis;
enum class StringUse { kLiteral, kPropertyName };
constexpr std::uint8_t kDirectNamedInstructions = 2;
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};

class BodyWriter {
 public:
  BodyWriter(ScalarNames names, std::ostream& output, EmitBudget budget,
             const MethodConstants* constants,
             ObjectBufferBudget object_budget,
             const BranchSourceFacts* facts = nullptr,
             std::span<const StagedNamedInvocation> staged_calls = {},
             StructuredExit exit = StructuredExit::kReturn)
      : names_(names),
        output_(output),
        remaining_(budget.bytes),
        constants_(constants),
        object_budget_(object_budget), facts_(facts),
        staged_calls_(staged_calls), exit_(exit) {}

  BodyEmitResult Write(const abc::MethodBody& body) {
    if (!body.tries.empty() || !body.handlers.empty())
      return BodyEmitFailure{BodyEmitError::kExceptionRegions, {}, {}, {}};
    return Sequence(body.instructions, false, true);
  }
  BodyEmitResult Instruction(const abc::BodyInstruction& instruction) {
    return Sequence(std::span(&instruction, 1), true, false);
  }
  BodyEmitResult Segment(std::span<const abc::BodyInstruction> instructions,
                         std::size_t first, bool initialized,
                         bool terminal) {
    return Sequence(instructions, initialized, terminal, first);
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

  BodyEmitResult StagedCallHead(const StagedNamedInvocation& invocation,
                              abc::CodeOffset offset) {
    offset_ = offset;
    try {
      Expression(ScalarValue{Accumulator{}});
      Text(" = (");
      Expression(ScalarValue{invocation.invoke.receiver});
      Text(")[");
      StringConstant(invocation.read.name, StringUse::kPropertyName);
      Text("]((() => {\n");
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{BodyEmitError::kExpression, offset_, {},
                             EmitError::kWrite};
    }
  }

  BodyEmitResult StagedCallEnd(const StagedNamedInvocation& invocation,
                              abc::CodeOffset offset) {
    offset_ = offset;
    if (invocation.invoke.Count() == 0)
      return Fail(BodyEmitError::kUnsupportedInstruction);
    try {
      Text("return ");
      Expression(ScalarValue{*invocation.invoke.At(0)});
      Text(";\n})()");
      for (std::uint32_t i = 1;
           i < invocation.invoke.Count() && !error_; ++i) {
        Text(", ");
        Expression(ScalarValue{*invocation.invoke.At(i)});
      }
      Text(");\n");
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{BodyEmitError::kExpression, offset_, {},
                             EmitError::kWrite};
    }
  }

  BodyEmitResult StagedCallClose(const StagedNamedInvocation& invocation,
                                abc::CodeOffset offset) {
    offset_ = offset;
    try {
      Text("})()");
      for (std::uint32_t i = 1;
           i < invocation.invoke.Count() && !error_; ++i) {
        Text(", ");
        Expression(ScalarValue{*invocation.invoke.At(i)});
      }
      Text(");\n");
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{BodyEmitError::kExpression, offset_, {},
                             EmitError::kWrite};
    }
  }

  BodyEmitResult FieldBatch(std::span<const FreshFieldSite> fields,
                            std::size_t first_ordinal,
                            abc::CodeOffset offset) {
    offset_ = offset;
    if (fields.empty()) return Fail(BodyEmitError::kUnsupportedInstruction);
    try {
      const auto object = fields.front().object;
      Expression(ScalarValue{object});
      Text(" = ({...");
      Expression(ScalarValue{object});
      for (std::size_t i = 0; i < fields.size() && !error_; ++i) {
        if (fields[i].first != first_ordinal || fields[i].object != object)
          return Fail(BodyEmitError::kUnsupportedInstruction);
        Text(", [");
        if (const auto* name =
                std::get_if<abc::PoolIndex>(&fields[i].key)) {
          StringConstant(*name);
        } else {
          std::array<char,
                     std::numeric_limits<std::uint32_t>::digits10 + 1> digits{};
          const auto index = std::get<std::uint32_t>(fields[i].key);
          const auto end = std::to_chars(digits.data(),
                                         digits.data() + digits.size(), index);
          Text({digits.data(),
                static_cast<std::size_t>(end.ptr - digits.data())});
        }
        Text("]: ");
        if (i + 1 == fields.size())
          Expression(ScalarValue{Accumulator{}});
        else
          FieldTemp(first_ordinal + i);
      }
      Text("});\n");
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{BodyEmitError::kExpression, offset_, {},
                             EmitError::kWrite};
    }
  }

  BodyEmitResult Ordered(const abc::MethodBody& body, const NormalFlow& graph,
                         const AccumulatorFlow& flow) {
    try {
      if (exit_ != StructuredExit::kReturn) {
        if (body.instructions.empty() ||
            body.instructions.back().decoded.opcode !=
                abc::Opcode::kRETURNUNDEFINED)
          return Fail(BodyEmitError::kMissingTerminator);
        synthetic_terminal_ = body.instructions.size() - 1;
      }
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
                     flow.entries[id.value] == AccumulatorState::kValue, false,
                     block.first_instruction);
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
                          bool accumulator_defined, bool require_terminator,
                          std::size_t first_instruction = 0) {
    bool terminated = false;
    auto staged = std::lower_bound(
        staged_calls_.begin(), staged_calls_.end(), first_instruction,
        [](const StagedNamedInvocation& call, std::size_t position) {
          return call.call < position;
        });
    auto deferred_head = staged;
    const StagedNamedInvocation* open_deferred = nullptr;
    try {
      for (std::size_t i = 0; i < instructions.size(); ++i) {
        const auto& instruction = instructions[i];
        offset_ = instruction.offset;
        if (terminated) return Fail(BodyEmitError::kTrailingInstructions);
        const auto position = first_instruction + i;
        while (deferred_head != staged_calls_.end() &&
               deferred_head->timing == SpilledCallTiming::kImmediate)
          ++deferred_head;
        if (deferred_head != staged_calls_.end() &&
            position == deferred_head->property) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          const auto result = StagedCallHead(*deferred_head, offset_);
          if (const auto* error = std::get_if<BodyEmitFailure>(&result))
            return *error;
          open_deferred = &*deferred_head++;
          accumulator_defined = true;
          continue;
        }
        if (staged != staged_calls_.end() &&
            staged->timing == SpilledCallTiming::kImmediate &&
            position == staged->property)
          continue;
        if (staged != staged_calls_.end() &&
            (position == staged->spill || position == staged->reload))
          continue;
        if (open_deferred &&
            (position == open_deferred->spill ||
             position == open_deferred->reload))
          continue;
        if (staged != staged_calls_.end() && position == staged->call) {
          const bool immediate =
              staged->timing == SpilledCallTiming::kImmediate;
          const auto result = immediate
                                  ? Invocation({staged->read, staged->invoke},
                                               offset_)
                                  : StagedCallEnd(*staged, offset_);
          if (const auto* error = std::get_if<BodyEmitFailure>(&result))
            return *error;
          if (!immediate) open_deferred = nullptr;
          accumulator_defined = true;
          ++staged;
          continue;
        }
        if (facts_ && position < facts_->language_loads.size() &&
            facts_->language_loads[position]) {
          const auto lexical = LiftLexicalInstruction(
              instruction.decoded, names_.registers.size());
          Expression(ScalarValue{Accumulator{}});
          Text(" = ");
          if (std::holds_alternative<LexicalLoad>(lexical)) {
            const auto found = std::lower_bound(
                facts_->captures.begin(), facts_->captures.end(), position,
                [](const CapturedLoadSite& site, std::size_t index) {
                  return site.instruction < index;
                });
            if (found == facts_->captures.end() ||
                found->instruction != position)
              return Fail(BodyEmitError::kUnsupportedInstruction);
            auto* buffer = dynamic_cast<CaptureBuffer*>(output_.rdbuf());
            if (!buffer || !buffer->Capture(found->cell))
              return Fail(BodyEmitError::kExpression);
          } else {
            const auto loaded = LiftModuleLoad(instruction.decoded);
            const auto* module = std::get_if<ModuleLoad>(&loaded);
            if (!module)
              return Fail(BodyEmitError::kUnsupportedInstruction);
            Text(module->space == ModuleSpace::kExternal ? "$import"
                                                           : "$local");
            Number(module->slot.value);
          }
          Text(";\n");
          accumulator_defined = true;
          if (error_) return *error_;
          continue;
        }
        if (facts_ && i != 0 && position != 0 &&
            position - 1 < facts_->language_loads.size() &&
            facts_->language_loads[position - 1] &&
            std::holds_alternative<ModuleLoad>(LiftModuleLoad(
                instructions[i - 1].decoded))) {
          const auto guard = LiftHoleInstruction(instruction.decoded,
                                                 names_.registers.size());
          if (const auto* checked = std::get_if<HoleGuard>(&guard);
              checked &&
              std::holds_alternative<Accumulator>(checked->value) &&
              std::holds_alternative<HoleNameSlot>(checked->name))
            continue;
        }
        using abc::Opcode;
        const auto opcode = instruction.decoded.opcode;
        const auto super = LiftSuperInstruction(
            instruction.decoded,
            static_cast<std::uint32_t>(names_.registers.size()));
        if (const auto* range = std::get_if<SuperRange>(&super)) {
          if (!names_.range_derived_constructor)
            return Fail(BodyEmitError::kUnsupportedInstruction);
          Expression(ScalarValue{Accumulator{}});
          Text(" = super(");
          for (std::uint32_t arg = 0; arg < range->count && !error_; ++arg) {
            if (arg != 0) Text(", ");
            Expression(ScalarValue{abc::RegisterId{
                range->first.value + arg}});
          }
          Text(");\n");
          accumulator_defined = true;
          if (error_) return *error_;
          continue;
        }
        if (std::holds_alternative<SuperCheck>(super)) {
          if (!names_.range_derived_constructor || !accumulator_defined)
            return Fail(BodyEmitError::kUnsupportedInstruction);
          continue;
        }
        if (const auto* error = std::get_if<SuperError>(&super);
            error && *error != SuperError::kUnsupportedOpcode)
          return Fail(BodyEmitError::kUnsupportedInstruction);
        if (opcode == Opcode::kNOP) continue;
        if (opcode == Opcode::kLDFUNCTION) {
          const auto lifted = LiftConstantObjectInstruction(instruction.decoded);
          const auto* operation =
              std::get_if<ConstantObjectInstruction>(&lifted);
          if (!operation ||
              operation->kind != ConstantObjectKind::kCurrentFunction)
            return Fail(BodyEmitError::kUnsupportedInstruction);
          if (names_.current_function.empty())
            return Fail(BodyEmitError::kCurrentFunctionBinding);
          Expression(ScalarValue{Accumulator{}});
          Text(" = ");
          Account(WriteScalarExpression(
              ScalarValue{Accumulator{}},
              {names_.current_function, {}}, output_, {remaining_}));
          Text(";\n");
          accumulator_defined = true;
          if (error_) return *error_;
          continue;
        }
        if (opcode == Opcode::kLDTHIS || opcode == Opcode::kDEBUGGER) {
          const auto lifted = LiftConstantObjectInstruction(instruction.decoded);
          const auto* operation =
              std::get_if<ConstantObjectInstruction>(&lifted);
          if (!operation)
            return Fail(BodyEmitError::kUnsupportedInstruction);
          if (operation->kind == ConstantObjectKind::kThis) {
            Expression(ScalarValue{Accumulator{}});
            Text(" = this;\n");
            accumulator_defined = true;
          } else if (operation->kind == ConstantObjectKind::kDebugger) {
            Text("debugger;\n");
          }
          if (error_) return *error_;
          continue;
        }
        if (opcode == Opcode::kCREATEITERRESULTOBJ_V8_V8) {
          if (constants_ &&
              constants_->header.profile != abc::ProfileId::kDynamic1301)
            return Fail(BodyEmitError::kUnsupportedInstruction);
          const auto created = LiftIterResult(instruction.decoded,
                                              names_.registers.size());
          const auto* result = std::get_if<IterResult>(&created);
          if (!result)
            return Fail(BodyEmitError::kUnsupportedInstruction);
          Expression(ScalarValue{Accumulator{}});
          Text(" = {value: ");
          Expression(ScalarValue{result->value});
          Text(", done: ");
          Expression(ScalarValue{result->done});
          Text(" === true};\n");
          accumulator_defined = true;
        } else if (opcode == Opcode::kCREATEEMPTYOBJECT ||
            opcode == Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16 ||
            opcode == Opcode::kCREATEOBJECTWITHBUFFER_IMM16_ID16) {
          const auto lifted = LiftObjectCreation(instruction.decoded);
          if (std::holds_alternative<ObjectCreationError>(lifted))
            return Fail(BodyEmitError::kUnsupportedInstruction);
          Expression(ScalarValue{Accumulator{}});
          Text(" = ");
          if (std::holds_alternative<EmptyObjectCreation>(lifted)) {
            Text("({})");
          } else if (!error_) {
            if (!constants_) return Fail(BodyEmitError::kMissingConstants);
            const auto result = WriteObjectBuffer(
                std::get<BufferedObjectCreation>(lifted), offset_, *constants_, output_,
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
        } else if (opcode == Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16 ||
                   opcode == Opcode::kTRYLDGLOBALBYNAME_IMM16_ID16) {
          GlobalLoad(instruction.decoded);
          accumulator_defined = true;
        } else if (opcode == Opcode::kLDOBJBYNAME_IMM8_ID16 ||
                   opcode == Opcode::kLDOBJBYNAME_IMM16_ID16) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          if (const auto invocation = MatchNamedInvocation(
                  instructions, i, names_.registers.size());
              invocation &&
              (exit_ != StructuredExit::kTryBreak ||
               invocation->instruction_count == kDirectNamedInstructions)) {
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
        } else if (opcode == Opcode::kSTOBJBYVALUE_IMM8_V8_V8 ||
                   opcode == Opcode::kSTOBJBYVALUE_IMM16_V8_V8) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          ValueStore(instruction.decoded);
        } else if (opcode == Opcode::kCALLARG0_IMM8 ||
                   opcode == Opcode::kCALLARG1_IMM8_V8 ||
                   opcode == Opcode::kCALLARGS2_IMM8_V8_V8 ||
                   opcode == Opcode::kCALLARGS3_IMM8_V8_V8_V8 ||
                   opcode == Opcode::kCALLRANGE_IMM8_IMM8_V8) {
          if (!accumulator_defined)
            return Fail(BodyEmitError::kAccumulatorHole);
          Call(instruction.decoded);
        } else if (opcode == Opcode::kNEWOBJRANGE_IMM8_IMM8_V8 ||
                   opcode == Opcode::kNEWOBJRANGE_IMM16_IMM8_V8 ||
                   opcode == Opcode::kWIDE_NEWOBJRANGE_PREF_IMM16_V8) {
          if (constants_ &&
              constants_->header.profile != abc::ProfileId::kDynamic1202 &&
              constants_->header.profile != abc::ProfileId::kDynamic1206 &&
              constants_->header.profile != abc::ProfileId::kDynamic1301)
            return Fail(BodyEmitError::kUnsupportedInstruction);
          Construct(instruction.decoded);
          accumulator_defined = true;
        } else if (opcode == Opcode::kCOPYRESTARGS_IMM8 ||
                   opcode == Opcode::kWIDE_COPYRESTARGS_PREF_IMM16) {
          const auto rest = LiftRestArguments(instruction.decoded);
          const auto* operation = std::get_if<RestCopy>(&rest);
          if (!operation || !names_.rest ||
              names_.rest->first_argument != operation->first_argument ||
              (constants_ &&
               constants_->header.profile != abc::ProfileId::kDynamic1202 &&
               constants_->header.profile != abc::ProfileId::kDynamic1206 &&
               constants_->header.profile != abc::ProfileId::kDynamic1301))
            return Fail(BodyEmitError::kUnsupportedInstruction);
          Expression(ScalarValue{Accumulator{}});
          Text(" = ");
          if (error_) return *error_;
          // Identifier validation stays with the scalar expression writer.
          const ScalarNames rest_names{names_.rest->name, {}};
          Account(WriteScalarExpression(ScalarValue{Accumulator{}},
                                        rest_names, output_, {remaining_}));
          Text(";\n");
          accumulator_defined = true;
        } else if (opcode == Opcode::kRETURNUNDEFINED) {
          if (synthetic_terminal_ && position == *synthetic_terminal_) {
            switch (exit_) {
              case StructuredExit::kPrefixBreak:
                Text("break $prefix_exit;\n");
                break;
              case StructuredExit::kTryBreak:
                Text("break $try_exit;\n");
                break;
              case StructuredExit::kHandlerBreak:
                Text("break $handler_exit;\n");
                break;
              case StructuredExit::kReturn:
                return Fail(BodyEmitError::kUnsupportedInstruction);
            }
          } else {
            Text("return;\n");
          }
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
  void GlobalLoad(const abc::DecodedInstruction& instruction) {
    const auto lifted = LiftGlobalAccess(instruction);
    const auto* load = std::get_if<GlobalAccess>(&lifted);
    if (!load || load->operation != GlobalOp::kReadName) {
      error_ = Fail(BodyEmitError::kUnsupportedInstruction);
      return;
    }
    if (!constants_ || !constants_->global_scope) {
      error_ = Fail(BodyEmitError::kGlobalScope);
      return;
    }
    const auto& context = *constants_;
    const auto reference = abc::ResolvePoolReference(
        context.file, context.header, context.index, context.method,
        load->name, abc::PoolEntityKind::kString);
    if (const auto* failure = std::get_if<abc::MetadataError>(&reference)) {
      error_ = BodyEmitFailure{BodyEmitError::kConstant, offset_, {}, {},
                               *failure};
      return;
    }
    const auto string = abc::ReadStringEntity(
        context.file, context.header,
        std::get<abc::PoolReference>(reference).offset, context.string_budget);
    if (const auto* failure = std::get_if<abc::MetadataError>(&string)) {
      error_ = BodyEmitFailure{BodyEmitError::kConstant, offset_, {}, {},
                               *failure};
      return;
    }
    const auto& name = std::get<std::u16string>(string);
    if (!AllowsGlobalReference(name, *context.global_scope)) {
      error_ = Fail(BodyEmitError::kGlobalReference);
      return;
    }
    Expression(ScalarValue{Accumulator{}});
    Text(" = ");
    constexpr std::size_t kNameChunkBytes = 256;
    std::array<char, kNameChunkBytes> chunk{};
    std::size_t used = 0;
    for (const auto ch : name) {
      chunk[used++] = static_cast<char>(ch);
      if (used == chunk.size()) {
        Text({chunk.data(), used});
        used = 0;
      }
    }
    Text({chunk.data(), used});
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
  void Construct(const abc::DecodedInstruction& instruction) {
    const auto lifted =
        LiftConstructorCall(instruction, names_.registers.size());
    if (const auto* error = std::get_if<ConstructorCallError>(&lifted)) {
      error_ = BodyEmitFailure{
          BodyEmitError::kUnsupportedInstruction, offset_,
          *error == ConstructorCallError::kRegisterRange
              ? ScalarError::kRegisterRange
              : ScalarError::kMalformedInstruction,
          {}};
      return;
    }
    const auto& call = std::get<ConstructorCall>(lifted);
    Expression(ScalarValue{Accumulator{}});
    Text(" = new (");
    Expression(ScalarValue{call.inputs.first});
    Text(")(");
    for (std::uint32_t i = 0; i < call.ArgumentCount() && !error_; ++i) {
      if (i) Text(", ");
      Expression(ScalarValue{*call.ArgumentAt(i)});
    }
    Text(");\n");
  }
  void NamedCall(const NamedInvocation& invocation) {
    // The typed match proves a private receiver register. A delayed pure-copy
    // match may no longer have that receiver in acc at this emission site.
    // Native Reference-call syntax keeps the original this and one getter.
    Expression(ScalarValue{Accumulator{}});
    Text(" = (");
    Expression(ScalarValue{invocation.call.receiver});
    Text(")[");
    StringConstant(invocation.property.name, StringUse::kPropertyName);
    Text("](");
    for (std::uint32_t i = 0; i < invocation.call.Count() && !error_;
         ++i) {
      if (i) Text(", ");
      Expression(ScalarValue{*invocation.call.At(i)});
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
    if (std::binary_search(names_.constructor_this_writes.begin(),
                           names_.constructor_this_writes.end(), offset_)) {
      Text("this[");
      StringConstant(store.name, StringUse::kPropertyName);
      Text("] = ");
      Expression(ScalarValue{Accumulator{}});
      Text(";\n");
      return;
    }
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
  void ValueStore(const abc::DecodedInstruction& instruction) {
    const auto lifted = LiftValueWrite(instruction, names_.registers.size());
    if (const auto* error = std::get_if<ValueWriteError>(&lifted)) {
      error_ = BodyEmitFailure{
          BodyEmitError::kUnsupportedInstruction, offset_,
          *error == ValueWriteError::kRegisterRange
              ? ScalarError::kRegisterRange
              : ScalarError::kMalformedInstruction,
          {}};
      return;
    }
    const auto& write = std::get<ValueWrite>(lifted);
    // Strict [[Set]] matches the pinned runtime, including inherited setters
    // and a throwing failure. The instruction does not replace acc.
    Text("(($object, $key, $value) => { \"use strict\"; ");
    Text("$object[$key] = $value; })(");
    Expression(ScalarValue{write.object});
    Text(", ");
    Expression(ScalarValue{write.key});
    Text(", ");
    Expression(ScalarValue{Accumulator{}});
    Text(");\n");
  }
  void FieldTemp(std::size_t ordinal) {
    std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> digits{};
    const auto end = std::to_chars(digits.data(), digits.data() +
                                                    digits.size(), ordinal);
    Text(kFieldTempPrefix);
    Text({digits.data(), static_cast<std::size_t>(end.ptr - digits.data())});
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
  void Number(std::uint32_t value) {
    std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1> digits{};
    const auto end = std::to_chars(digits.data(), digits.data() +
                                  digits.size(), value);
    Text({digits.data(), static_cast<std::size_t>(end.ptr - digits.data())});
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
  const BranchSourceFacts* facts_;
  std::span<const StagedNamedInvocation> staged_calls_;
  StructuredExit exit_;
  std::optional<std::size_t> synthetic_terminal_;
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

BodyEmitResult WriteFreshFieldBatch(
    std::span<const FreshFieldSite> fields, std::size_t first_ordinal,
    abc::CodeOffset offset, ScalarNames names, std::ostream& output,
    EmitBudget budget, const MethodConstants* constants) {
  return BodyWriter(names, output, budget, constants, {})
      .FieldBatch(fields, first_ordinal, offset);
}

BodyEmitResult WriteStagedCallHead(
    const analysis::StagedNamedInvocation& invocation, abc::CodeOffset offset,
    ScalarNames names, std::ostream& output, EmitBudget budget,
    const MethodConstants* constants) {
  return BodyWriter(names, output, budget, constants, {})
      .StagedCallHead(invocation, offset);
}

BodyEmitResult WriteStagedCallEnd(
    const analysis::StagedNamedInvocation& invocation, abc::CodeOffset offset,
    ScalarNames names, std::ostream& output, EmitBudget budget) {
  return BodyWriter(names, output, budget, nullptr, {})
      .StagedCallEnd(invocation, offset);
}

BodyEmitResult WriteStagedCallClose(
    const StagedNamedInvocation& invocation, abc::CodeOffset offset,
    ScalarNames names, std::ostream& output, EmitBudget budget) {
  return BodyWriter(names, output, budget, nullptr, {})
      .StagedCallClose(invocation, offset);
}

BodyEmitResult WriteNamedInvocation(const analysis::NamedInvocation& invocation,
                                    abc::CodeOffset offset, ScalarNames names,
                                    std::ostream& output, EmitBudget budget,
                                    const MethodConstants* constants) {
  return BodyWriter(names, output, budget, constants, {})
      .Invocation(invocation, offset);
}

std::optional<BodyEmitFailure> ValidateGlobalScalarNames(
    const abc::MethodBody& body, ScalarNames names,
    const MethodConstants* constants) {
  if (!constants || !constants->global_scope) return {};
  bool reads_global = false;
  for (const auto& instruction : body.instructions) {
    const auto access = LiftGlobalAccess(instruction.decoded);
    if (const auto* global = std::get_if<GlobalAccess>(&access);
        global && global->operation == GlobalOp::kReadName) {
      reads_global = true;
      break;
    }
  }
  if (!reads_global) return {};
  if (!names.accumulator.starts_with('$'))
    return BodyEmitFailure{BodyEmitError::kGlobalReference, {}};
  for (const auto name : names.registers)
    if (!name.starts_with('$'))
      return BodyEmitFailure{BodyEmitError::kGlobalReference, {}};
  return {};
}

BodyEmitResult WriteLinearBody(const abc::MethodBody& body, ScalarNames names,
                               std::ostream& output, EmitBudget budget,
                               const MethodConstants* constants,
                               ObjectBufferBudget object_budget) {
  if (const auto failure = ValidateGlobalScalarNames(body, names, constants))
    return *failure;
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
                                ObjectBufferBudget object_budget,
                                const BranchSourceFacts* facts,
                                std::span<const StagedNamedInvocation>
                                    staged_calls,
                                StructuredExit exit) {
  if (const auto failure = ValidateGlobalScalarNames(body, names, constants))
    return *failure;
  CaptureDiscard buffer;
  std::ostream discard(&buffer);
  const auto checked =
      BodyWriter(names, discard, budget, constants, object_budget, facts,
                 staged_calls, exit)
          .Ordered(body, graph, flow);
  if (std::holds_alternative<BodyEmitFailure>(checked)) return checked;
  return BodyWriter(names, output, budget, constants, object_budget, facts,
                    staged_calls, exit)
      .Ordered(body, graph, flow);
}

BodyEmitResult WriteBranchSegment(
    std::span<const abc::BodyInstruction> instructions, std::size_t first,
    bool initialized, bool terminal, ScalarNames names,
    std::ostream& output, EmitBudget budget,
    const MethodConstants* constants, const BranchSourceFacts& facts) {
  return BodyWriter(names, output, budget, constants, {}, &facts)
      .Segment(instructions, first, initialized, terminal);
}
}  // namespace abcd::source
