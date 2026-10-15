#include "default_signature.h"

#include <limits>
#include <new>
#include <bit>
#include <stdexcept>

#include "tree_checks.h"
#include "tree_flow.h"

namespace abcd::source {
namespace {
using Error = DefaultSignatureError;
void Require(bool condition, Error error) { if (!condition) throw error; }
TreeLiteral Literal(const analysis::ValueOperand& operand) {
  if (const auto* integer = std::get_if<analysis::IntegerLiteral>(&operand))
    return IntegerLiteral{integer->value};
  if (const auto* number = std::get_if<analysis::Float64Bits>(&operand))
    return NumberLiteral{number->value};
  const auto* special = std::get_if<analysis::SpecialValue>(&operand);
  Require(special != nullptr, Error::kLiteral);
  switch (*special) {
    case analysis::SpecialValue::kUndefined: return UndefinedLiteral{};
    case analysis::SpecialValue::kNull: return NullLiteral{};
    case analysis::SpecialValue::kTrue: return BooleanLiteral{true};
    case analysis::SpecialValue::kFalse: return BooleanLiteral{false};
    case analysis::SpecialValue::kNaN:
    case analysis::SpecialValue::kInfinity: throw Error::kLiteral;
  }
  throw Error::kLiteral;
}
template <class T>
void Grow(std::vector<T>& items, std::size_t extra, TreeCheckBudget& budget) {
  constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
  Require(items.size() <= kMaxId && extra <= kMaxId - items.size(), Error::kResourceLimit);
  const auto count = items.size() + extra;
  if (count <= items.capacity()) return;
  // Retain old storage in the peak charge while reserve replaces the arena.
  budget.Admit(count, sizeof(T));
  items.reserve(count);
  budget.Admit(items.capacity() - count, sizeof(T));
}
}  // namespace

std::variant<DefaultSignatureProof, DefaultSignatureError> BuildDefaultSignature(
    const DefaultBody& body, FunctionTree& tree, analysis::SemanticBudget budget, const DefaultSignatureContext* capture) {
  try {
    const auto& graph = body.Graph();
    Require(tree.value_revision == graph.revision && tree.cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(tree.signature.empty(), Error::kPlacement);
    const auto storage = MeasureTreeStorage(tree, budget);
    const auto* measured = std::get_if<TreeStorage>(&storage);
    if (!measured) return std::get<TreeStorageError>(storage) == TreeStorageError::kWorkLimit
        ? Error::kWorkLimit : Error::kResourceLimit;
    TreeCheckBudget checked(budget);
    checked.Admit(measured->bytes, 1); checked.Step(measured->work);
    for (const auto& parameter : tree.parameters) {
      checked.Step(); Require(!parameter.initializer && !parameter.rest, Error::kParameter);
    }
    std::optional<SymbolId> captured_symbol;
    if (const auto* binding = body.Capture()) {
      Require(capture && capture->evaluation.cells, Error::kContext);
      const auto result = VerifyTreeFlow(graph, capture->control, capture->evaluation, tree, checked.Remaining());
      const auto* flow = std::get_if<TreeFlow>(&result);
      if (!flow) {
        const auto error = std::get<TreeFlowError>(result);
        if (error == TreeFlowError::kResourceLimit) throw Error::kResourceLimit;
        if (error == TreeFlowError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kContext;
      }
      checked.Admit(flow->allocated_bytes,1); checked.Step(flow->work);
      const auto cell = FindCell(capture->evaluation.cells->cells, binding->storage);
      Require(cell && cell->value < flow->cells.size() && flow->cells[cell->value], Error::kContext);
      captured_symbol = flow->cells[cell->value];
    }
    std::vector<std::optional<SymbolId>> joint_symbols;
    if(body.CellParameters()) {
      checked.Array(joint_symbols,tree.parameters.size());
      Require(capture&&capture->evaluation.cells,Error::kContext);
      auto flow=VerifyTreeFlow(graph,capture->control,capture->evaluation,tree,checked.Remaining());
      const auto* proof=std::get_if<TreeFlow>(&flow);
      if(!proof){const auto error=std::get<TreeFlowError>(flow);throw error==TreeFlowError::kResourceLimit?Error::kResourceLimit:error==TreeFlowError::kWorkLimit?Error::kWorkLimit:Error::kContext;}
      checked.Admit(proof->allocated_bytes,1);checked.Step(proof->work);
      for(const auto& item:body.SignatureCaptures()) {
        checked.Step(std::bit_width(capture->evaluation.cells->cells.cells.size())+1);
        const auto cell=FindCell(capture->evaluation.cells->cells,item.storage);
        Require(item.parameter<joint_symbols.size()&&cell&&cell->value<proof->cells.size()&&proof->cells[cell->value],Error::kContext);
        joint_symbols[item.parameter]=proof->cells[cell->value];
      }
    }
    Grow(tree.expressions, body.Signature().Defaults().size(), checked);
    Grow(tree.signature, body.Signature().Operations().size(), checked);
    for (const auto& candidate : body.Signature().Defaults()) {
      checked.Step();
      Require(candidate.parameter < tree.parameters.size() && candidate.initializer.value < graph.ops.size(),
              Error::kParameter);
      const auto origin = graph.ops[candidate.initializer.value].origin;
      const ExprId expression{static_cast<std::uint32_t>(tree.expressions.size())};
      if (candidate.text) {
        Require(capture && capture->constants && candidate.initializer.value < capture->constants->operations.size(), Error::kLiteral);
        const auto* text = std::get_if<TextId>(&capture->constants->operations[candidate.initializer.value]);
        Require(text && text->value < tree.texts.size(), Error::kLiteral);
        tree.expressions.push_back({TreeLiteral{*text},{origin.value}});
      } else if(body.CaptureParameter(candidate.parameter)) {
        Require(joint_symbols[candidate.parameter].has_value(),Error::kContext);
        tree.expressions.push_back({SymbolExpr{*joint_symbols[candidate.parameter]},{origin.value}});
      } else if (body.Capture() && body.Capture()->candidate.shape.parameter == candidate.parameter) {
        Require(captured_symbol.has_value(), Error::kContext);
        tree.expressions.push_back({SymbolExpr{*captured_symbol}, {origin.value}});
      } else tree.expressions.push_back({Literal(candidate.fallback), {origin.value}});
      tree.parameters[candidate.parameter].initializer = expression;
    }
    for (const auto placement : body.Signature().Operations()) {
      checked.Step(); Require(placement.operation.value < graph.ops.size(), Error::kPlacement);
      tree.signature.push_back({placement.operation, {placement.parameter},
                                {graph.ops[placement.operation.value].origin.value}});
    }
    // Conservatively keep construction allocations charged during independent
    // validation. No default becomes publishable solely by this constructor.
    const auto verified = VerifyDefaultSignature(body, tree, checked.Remaining(), capture);
    const auto* proof = std::get_if<DefaultSignatureProof>(&verified);
    if (!proof) return std::get<Error>(verified);
    checked.Admit(proof->allocated_bytes, 1); checked.Step(proof->work);
    return DefaultSignatureProof{graph.revision, graph.cfg_revision, checked.Bytes(), checked.Work()};
  } catch (Error error) { return error;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
