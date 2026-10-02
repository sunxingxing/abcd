#include "default_signature.h"

#include <new>
#include <stdexcept>

#include "tree_checks.h"
#include "tree_flow.h"

namespace abcd::source {
namespace {
using Error = DefaultSignatureError;
void Require(bool condition, Error error) { if (!condition) throw error; }
bool SameLiteral(const TreeLiteral& syntax, const analysis::ValueOperand& value) {
  return std::visit([&](const auto& literal) {
    using T = std::decay_t<decltype(literal)>;
    if constexpr (std::is_same_v<T, IntegerLiteral>) {
      const auto* expected = std::get_if<analysis::IntegerLiteral>(&value);
      return expected && literal.value == expected->value;
    } else if constexpr (std::is_same_v<T, NumberLiteral>) {
      const auto* expected = std::get_if<analysis::Float64Bits>(&value);
      return expected && literal.bits == expected->value;
    } else if constexpr (std::is_same_v<T, TextId>) {
      return false; // Pool-backed default strings need a separate resolution proof.
    } else {
      const auto* expected = std::get_if<analysis::SpecialValue>(&value);
      if (!expected) return false;
      if constexpr (std::is_same_v<T, UndefinedLiteral>) return *expected == analysis::SpecialValue::kUndefined;
      else if constexpr (std::is_same_v<T, NullLiteral>) return *expected == analysis::SpecialValue::kNull;
      else {
        static_assert(std::is_same_v<T, BooleanLiteral>);
        return *expected == (literal.value ? analysis::SpecialValue::kTrue : analysis::SpecialValue::kFalse);
      }
    }
  }, syntax);
}
class SignatureCheck {
 public:
  SignatureCheck(const DefaultBody& body, const FunctionTree& tree, TreeCheckBudget& budget, const DefaultCaptureContext* capture)
      : body_(body), tree_(tree), budget_(budget), capture_(capture) {}
  void Run() {
    const auto& graph = body_.Graph();
    const bool ordinary = tree_.callable == CallableKind::kMethod ||
        tree_.callable == CallableKind::kFunction || tree_.callable == CallableKind::kArrow;
    // Literal and proved incoming-cell defaults introduce no invocation references.
    // The owner must still prove the strict enclosing source context; this
    // signature check does not authorize a sloppy standalone wrapper.
    const bool constructor = tree_.callable == CallableKind::kBaseConstructor ||
        tree_.callable == CallableKind::kDerivedConstructor;
    // Literal and outer-cell initializers do not read the current receiver.
    // Owner/ABI and incoming capture binding remain separate obligations.
    Require((tree_.callable == CallableKind::kMethod || constructor ||
        ((body_.Storage() == DefaultStorage::kCell || body_.Capture()) && ordinary)) &&
        tree_.strictness == Strictness::kStrict && !tree_.self,
            Error::kContext);
    std::optional<SymbolId> expected_capture;
    if (const auto* binding = body_.Capture()) {
      Require(capture_ && capture_->evaluation.cells, Error::kContext);
      const auto result = VerifyTreeFlow(graph, capture_->control, capture_->evaluation, tree_, budget_.Remaining());
      const auto* flow = std::get_if<TreeFlow>(&result);
      if (!flow) {
        const auto error = std::get<TreeFlowError>(result);
        if (error == TreeFlowError::kResourceLimit) throw Error::kResourceLimit;
        if (error == TreeFlowError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kContext;
      }
      budget_.Admit(flow->allocated_bytes,1); budget_.Step(flow->work);
      const auto cell = FindCell(capture_->evaluation.cells->cells, binding->storage);
      Require(cell && cell->value < flow->cells.size() && flow->cells[cell->value], Error::kContext);
      expected_capture = flow->cells[cell->value];
      Require(expected_capture->value < tree_.symbols.size() &&
          tree_.symbols[expected_capture->value].kind == BindingKind::kCapture, Error::kContext);
    }
    std::vector<const DefaultInitializer*> formals;
    std::vector<std::optional<ParameterId>> operations;
    std::vector<std::uint8_t> initializers;
    std::vector<std::uint8_t> moved;
    budget_.Array(formals, tree_.parameters.size());
    budget_.Array(operations, graph.ops.size());
    budget_.Array(initializers, tree_.expressions.size());
    budget_.Array(moved, tree_.origins.size());
    for (const auto& candidate : body_.Signature().Defaults()) {
      budget_.Step();
      Require(candidate.parameter < formals.size() && !formals[candidate.parameter], Error::kParameter);
      formals[candidate.parameter] = &candidate;
    }
    for (std::size_t i = 0; i < tree_.parameters.size(); ++i) {
      budget_.Step();
      const auto& parameter = tree_.parameters[i];
      Require(parameter.initializer.has_value() == (formals[i] != nullptr), Error::kParameter);
      if (!formals[i]) continue;
      const auto id = *parameter.initializer;
      const auto& node = tree_.expressions[id.value]; // Structure verified first.
      const auto* literal = std::get_if<TreeLiteral>(&node.expression);
      if (body_.Capture() && body_.Capture()->candidate.shape.parameter == i) {
        const auto* symbol = std::get_if<SymbolExpr>(&node.expression);
        Require(symbol && expected_capture == symbol->symbol, Error::kContext);
      } else Require(literal && SameLiteral(*literal, formals[i]->fallback), Error::kLiteral);
      Require(formals[i]->initializer.value < graph.ops.size() &&
          node.origin.value == graph.ops[formals[i]->initializer.value].origin.value, Error::kOrigin);
      initializers[id.value] = 1;
    }
    for (const auto placement : body_.Signature().Operations()) {
      budget_.Step();
      Require(placement.operation.value < operations.size() && !operations[placement.operation.value], Error::kPlacement);
      operations[placement.operation.value] = ParameterId{placement.parameter};
    }
    Require(tree_.signature.size() == body_.Signature().Operations().size(), Error::kPlacement);
    for (const auto placement : tree_.signature) {
      budget_.Step();
      Require(placement.operation.value < operations.size() &&
          operations[placement.operation.value] == placement.parameter, Error::kPlacement);
      operations[placement.operation.value].reset();
      const auto& op = graph.ops[placement.operation.value];
      Require(op.life == analysis::ValueLife::kRemoved && placement.origin.value == op.origin.value &&
          op.origin.value < graph.origins.size(), Error::kOrigin);
      const auto& input = graph.origins[op.origin.value];
      const auto* site = std::get_if<OriginalSite>(&tree_.origins[placement.origin.value]);
      Require(site && input.disposition == analysis::OriginDisposition::kRetained &&
          site->site == input.site && site->pc == input.pc && site->original == input.original, Error::kOrigin);
      moved[placement.origin.value] = 1;
    }
    // Backward origin DAG edges permit one forward propagation. Never expand
    // every expression's transitive PC set, which can be quadratic or worse.
    for (std::size_t i = 0; i < tree_.origins.size(); ++i) {
      budget_.Step();
      if (const auto* combined = std::get_if<CombinedOrigin>(&tree_.origins[i])) {
        for (std::uint32_t j = 0; j < combined->part_count; ++j) {
          budget_.Step();
          moved[i] |= moved[tree_.origin_parts[combined->first_part + j].value];
        }
      }
    }
    for (std::size_t i = 0; i < tree_.expressions.size(); ++i) {
      budget_.Step();
      Require(initializers[i] || !moved[tree_.expressions[i].origin.value], Error::kOrigin);
    }
    for (const auto& statement : tree_.statements) {
      budget_.Step(); Require(!moved[statement.origin.value], Error::kOrigin);
    }
    for (const auto& anchor : tree_.anchors) {
      budget_.Step(); Require(!moved[anchor.origin.value], Error::kOrigin);
    }
  }
 private:
  const DefaultBody& body_;
  const FunctionTree& tree_;
  TreeCheckBudget& budget_;
  const DefaultCaptureContext* capture_;
};
}  // namespace

std::variant<DefaultSignatureProof, DefaultSignatureError> VerifyDefaultSignature(
    const DefaultBody& body, const FunctionTree& tree, analysis::SemanticBudget budget, const DefaultCaptureContext* capture) {
  try {
    Require(tree.value_revision == body.Graph().revision && tree.cfg_revision == body.Graph().cfg_revision,
            Error::kRevision);
    const auto structure = VerifyTreeStructure(tree, budget);
    const auto* valid = std::get_if<TreeStructure>(&structure);
    if (!valid) {
      const auto error = std::get<TreeStructureError>(structure);
      if (error == TreeStructureError::kResourceLimit) return Error::kResourceLimit;
      if (error == TreeStructureError::kWorkLimit) return Error::kWorkLimit;
      return Error::kStructure;
    }
    TreeCheckBudget checked(budget);
    checked.Admit(valid->allocated_bytes, 1); checked.Step(valid->work);
    SignatureCheck(body, tree, checked, capture).Run();
    return DefaultSignatureProof{tree.value_revision, tree.cfg_revision, checked.Bytes(), checked.Work()};
  } catch (Error error) { return error;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit ? Error::kWorkLimit : Error::kResourceLimit;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
