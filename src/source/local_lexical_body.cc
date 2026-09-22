#include "abcd/source/local_lexical_body.h"

#include <array>
#include <charconv>
#include <limits>
#include <ostream>
#include <streambuf>

#include "leaf_sources.h"
#include "linear_instruction.h"

namespace abcd::source {
namespace {
constexpr std::string_view kCellPrefix = "$lex";
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};
class LexicalWriter {
 public:
  LexicalWriter(ScalarNames names, std::ostream& output, EmitBudget budget,
                const MethodConstants* constants, const LeafSources* functions)
      : names_(names),
        output_(output),
        remaining_(budget.bytes),
        constants_(constants),
        functions_(functions) {}
  BodyEmitResult Write(const abc::MethodBody& body,
                       const analysis::LinearLexicals& plan) {
    try {
      for (const auto cell : plan.Cells()) {
        Text("let ");
        Cell(cell);
        Text(";\n");
      }
      for (std::size_t i = 0; i < plan.Statements().size() && !error_; ++i) {
        offset_ = body.instructions[i].offset;
        const auto& statement = plan.Statements()[i];
        if (std::holds_alternative<analysis::KeepInstruction>(statement)) {
          const auto result = WriteLinearInstruction(
              body.instructions[i], names_, output_, {remaining_}, constants_);
          if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
            error_ = *failure;
          else
            Count(std::get<std::size_t>(result));
        } else if (std::holds_alternative<analysis::LocalFunction>(statement)) {
          Slot(analysis::Accumulator{});
          Text(" = ");
          Text(functions_->Source(next_function_++));
          Text(";\n");
        } else if (const auto* load =
                       std::get_if<analysis::LocalLoad>(&statement)) {
          Slot(analysis::Accumulator{});
          Text(" = ");
          Cell(plan.Cells()[load->cell.value]);
          Text(";\n");
        } else if (const auto* store =
                       std::get_if<analysis::LocalStore>(&statement)) {
          Cell(plan.Cells()[store->cell.value]);
          Text(" = ");
          Slot(store->value);
          Text(";\n");
        }
      }
      if (error_) return *error_;
      return written_;
    } catch (const std::ios_base::failure&) {
      return BodyEmitFailure{
          BodyEmitError::kExpression, offset_, {}, EmitError::kWrite};
    }
  }

 private:
  void Count(std::size_t bytes) {
    remaining_ -= bytes;
    written_ += bytes;
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
    Count(text.size());
  }
  void Number(std::uint32_t value) {
    std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1> text{};
    const auto end =
        std::to_chars(text.data(), text.data() + text.size(), value).ptr;
    Text(std::string_view(text.data(), end - text.data()));
  }
  void Cell(analysis::LocalCell cell) {
    Text(kCellPrefix);
    Number(cell.environment.value);
    Text("_");
    Number(cell.slot.value);
  }
  void Slot(analysis::ScalarSlot slot) {
    if (error_) return;
    const auto result = std::visit(
        [&](const auto& value) {
          return WriteScalarExpression(analysis::ScalarValue{value}, names_,
                                       output_, {remaining_});
        },
        slot);
    if (const auto* failure = std::get_if<EmitError>(&result))
      error_ =
          BodyEmitFailure{BodyEmitError::kExpression, offset_, {}, *failure};
    else
      Count(std::get<std::size_t>(result));
  }
  ScalarNames names_;
  std::ostream& output_;
  std::size_t remaining_;
  const MethodConstants* constants_;
  const LeafSources* functions_;
  std::size_t next_function_ = 0;
  std::size_t written_ = 0;
  abc::CodeOffset offset_{};
  std::optional<BodyEmitFailure> error_;
};
}  // namespace
LocalLexicalResult WriteLocalLexicalBody(const abc::MethodBody& body,
                                         ScalarNames names,
                                         std::ostream& output,
                                         LocalLexicalBudget budget,
                                         const MethodConstants* constants,
                                         const abc::MethodLookup* methods) {
  const auto resource = [] {
    return analysis::LinearLexicalFailure{
        analysis::LinearLexicalError::kResourceLimit, {}};
  };
  if (names.accumulator.starts_with(kCellPrefix))
    return BodyEmitFailure{
        BodyEmitError::kExpression, {}, {}, EmitError::kInvalidBinding};
  for (const auto name : names.registers)
    if (name.starts_with(kCellPrefix))
      return BodyEmitFailure{
          BodyEmitError::kExpression, {}, {}, EmitError::kInvalidBinding};
  try {
    DiscardBuffer buffer;
    std::ostream discard(&buffer);
    std::optional<LeafSources> functions;
    std::span<const analysis::IndependentCreation> creations;
    if (methods) {
      if (!constants)
        return BodyEmitFailure{BodyEmitError::kMissingConstants, {}, {}, {}};
      auto prepared = LeafSources::Build(body, *constants, *methods, budget);
      if (const auto* failure = std::get_if<BodyEmitFailure>(&prepared))
        return *failure;
      functions = std::get<LeafSources>(std::move(prepared));
      if (functions->AllocatedBytes() > budget.bytes) return resource();
      budget.bytes -= functions->AllocatedBytes();
      creations = functions->Facts();
    }
    const auto built = analysis::BuildNormalFlow(body, {budget.bytes});
    if (const auto* failure = std::get_if<analysis::FlowFailure>(&built))
      return *failure;
    const auto& graph = std::get<analysis::NormalFlow>(built);
    const auto graph_bytes =
        graph.blocks.capacity() * sizeof(analysis::FlowBlock);
    if (graph_bytes > budget.bytes) return resource();
    const auto bound = analysis::AnalyzeEnvironments(
        body, graph, names.registers.size(), {budget.bytes - graph_bytes});
    if (const auto* failure =
            std::get_if<analysis::EnvironmentFlowFailure>(&bound))
      return *failure;
    const auto& environments = std::get<analysis::EnvironmentFlow>(bound);
    if (environments.allocated_bytes > budget.bytes - graph_bytes)
      return resource();
    analysis::IdfQuota quota{budget.work};
    const auto planned = analysis::LinearLexicals::Build(
        body, environments, names.registers.size(), quota,
        {budget.bytes - graph_bytes - environments.allocated_bytes}, creations);
    if (const auto* failure =
            std::get_if<analysis::LinearLexicalFailure>(&planned))
      return *failure;
    const auto& plan = std::get<analysis::LinearLexicals>(planned);
    const auto checked = LexicalWriter(names, discard, budget.output, constants,
                                       functions ? &*functions : nullptr)
                             .Write(body, plan);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&checked))
      return *failure;
    const auto result = LexicalWriter(names, output, budget.output, constants,
                                      functions ? &*functions : nullptr)
                            .Write(body, plan);
    if (const auto* failure = std::get_if<BodyEmitFailure>(&result))
      return *failure;
    return std::get<std::size_t>(result);
  } catch (const std::bad_alloc&) {
    return resource();
  } catch (const std::length_error&) {
    return resource();
  }
}
}  // namespace abcd::source
