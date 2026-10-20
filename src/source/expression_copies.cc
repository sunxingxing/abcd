#include "expression_copies.h"

#include <bit>
#include <limits>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = ExpressionError;
void Require(bool valid, Error error) { if (!valid) throw error; }
class CopyExpressions {
 public:
  CopyExpressions(const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
                  ExpressionForest& forest, FunctionOwnerId owner, std::uint64_t work, std::span<const std::uint8_t> selected_edges)
      : graph_(graph), evaluation_(evaluation), forest_(forest), owner_(owner), work_(work), selected_edges_(selected_edges) {}
  void Append() {
    if (!evaluation_.phis) return;
    for (std::size_t b = 0; b < evaluation_.phis->bundles.size(); ++b) {
      Step();
      const auto& bundle = evaluation_.phis->bundles[b];
      const auto* edge = std::get_if<analysis::EdgeId>(&bundle.incoming);
      if (edge && !selected_edges_.empty() && selected_edges_[edge->value]) continue;
      if (evaluation_.exceptions) {
        if (const auto* placement = std::get_if<ThrowCopies>(&evaluation_.exceptions->placements[b])) {
          bool ordinary=false;
          for(std::size_t i=bundle.first;i<std::size_t(bundle.first)+bundle.count;++i) {
            Step();ordinary|=evaluation_.exceptions->projection!=ExceptionCopyProjection::kForOfClose || evaluation_.phis->projection[i]!=PhiCopyUse::kForOfProtocol;
          }
          if(ordinary) {
            const auto origin = Origin(edge ? TreeOrigin{EdgeOrigin{*edge}} : TreeOrigin{EntryOrigin{}});
            Stage(bundle, *placement, origin);
          } else Push(forest_.throws,ThrowExpressions{*placement,
              static_cast<std::uint32_t>(forest_.throw_assignments.size()),0});
          continue;
        }
      }
      const auto first = static_cast<std::uint32_t>(forest_.edge_assignments.size());
      // Parallel-copy scheduling may erase every self-assignment on a loop
      // backedge. Retain its empty placement range, but do not create an orphan
      // syntax origin when no assignment expression will consume it.
      if (bundle.schedule.steps.empty()) {
        Push(forest_.edges, EdgeExpressions{bundle.incoming, first, 0});
        continue;
      }
      const auto origin = Origin(edge ? TreeOrigin{EdgeOrigin{*edge}} : TreeOrigin{EntryOrigin{}});
      for (const auto& copy : bundle.schedule.steps) {
        Step();
        const auto* temporary = std::get_if<CopyTempId>(&copy.destination);
        const auto destination = temporary ? CycleSymbol(b, *temporary)
            : PhiSymbol(std::get<analysis::ValueId>(copy.destination));
        const auto* saved = std::get_if<CopyTempId>(&copy.source);
        const auto source = saved ? Add(SymbolExpr{CycleSymbol(b, *saved)}, origin)
            : Operand(std::get<analysis::ValueOperand>(copy.source), origin);
        Push(forest_.edge_assignments, Add(AssignmentExpr{SymbolTarget{destination}, source}, origin));
      }
      Push(forest_.edges, EdgeExpressions{bundle.incoming, first,
          static_cast<std::uint32_t>(bundle.schedule.steps.size())});
    }
    if (evaluation_.exceptions)
      for (const auto& commit : evaluation_.exceptions->commits) Commit(commit);
  }
  std::uint64_t Remaining() const { return work_; }
 private:
  void Step() { Require(work_ != 0, Error::kWorkLimit); --work_; }
  template <typename T> void Push(std::vector<T>& items, T value) {
    Require(items.size() < items.capacity(), Error::kResourceLimit);
    items.push_back(std::move(value));
  }
  ExprId Add(TreeExpression expression, TreeOriginId origin) {
    Step();
    const ExprId id{static_cast<std::uint32_t>(forest_.expressions.size())};
    Push(forest_.expressions, ExprNode{std::move(expression), origin});
    return id;
  }
  TreeOriginId Origin(TreeOrigin value) {
    Step();
    const TreeOriginId id{static_cast<std::uint32_t>(forest_.origins.size())};
    Push(forest_.origins, std::move(value));
    return id;
  }
  SymbolId CycleSymbol(std::size_t bundle, CopyTempId temporary) {
    Require(bundle < forest_.bindings.cycle_bases.size(), Error::kBinding);
    const auto base = forest_.bindings.cycle_bases[bundle];
    Require(base && base->value < forest_.bindings.symbols.size() &&
        temporary.value < forest_.bindings.symbols.size() - base->value, Error::kBinding);
    const SymbolId symbol{base->value + temporary.value};
    const auto* binding = std::get_if<CycleBinding>(&forest_.bindings.symbols[symbol.value]);
    Require(binding && binding->bundle == bundle && binding->temporary == temporary, Error::kBinding);
    return symbol;
  }
  SymbolId PhiSymbol(analysis::ValueId value) {
    Require(value.value < forest_.bindings.values.size(), Error::kBinding);
    const auto* symbol = std::get_if<SymbolId>(&forest_.bindings.values[value.value]);
    Require(symbol && symbol->value < forest_.bindings.symbols.size(), Error::kBinding);
    const auto* phi = std::get_if<PhiBinding>(&forest_.bindings.symbols[symbol->value]);
    Require(phi && phi->value == value, Error::kBinding);
    return *symbol;
  }
  SymbolId SlotSymbol(CatchSlotId slot) {
    Require(slot.value < forest_.bindings.catch_slots.size(), Error::kBinding);
    const auto symbol = forest_.bindings.catch_slots[slot.value];
    Require(symbol.value < forest_.bindings.symbols.size(), Error::kBinding);
    const auto* binding = std::get_if<CatchSlotBinding>(&forest_.bindings.symbols[symbol.value]);
    Require(binding && binding->slot == slot, Error::kBinding);
    return symbol;
  }
  ExprId Operand(const analysis::ValueOperand& operand, TreeOriginId origin) {
    // Most inputs read existing bindings. A checked sole-use tail expression
    // instead transfers ownership to this assignment without a snapshot.
    return std::visit([&](const auto& value) -> ExprId {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, analysis::ValueId>) {
        Require(value.value < forest_.bindings.values.size(), Error::kBinding);
        const auto& binding = forest_.bindings.values[value.value];
        if (const auto* symbol = std::get_if<SymbolId>(&binding)) return Add(SymbolExpr{*symbol}, origin);
        const auto* producer = std::get_if<analysis::OpResult>(&graph_.values[value.value].origin);
        if (producer && evaluation_.operations[producer->op.value].kind == EvaluationKind::kPhiInput) {
          Require(forest_.operations[producer->op.value].has_value(), Error::kUnavailableValue);
          return *forest_.operations[producer->op.value];
        }
        Require(std::holds_alternative<ReceiverEntryBinding>(binding) ||
            std::holds_alternative<NativeReceiverBinding>(binding), Error::kUnavailableValue);
        return Add(ThisExpr{owner_}, origin);
      } else if constexpr (std::is_same_v<T, analysis::IntegerLiteral>) {
        return Add(TreeLiteral{IntegerLiteral{value.value}}, origin);
      } else if constexpr (std::is_same_v<T, analysis::Float64Bits>) {
        return Add(TreeLiteral{NumberLiteral{value.value}}, origin);
      } else {
        static_assert(std::is_same_v<T, analysis::SpecialValue>);
        switch (value) {
          case analysis::SpecialValue::kUndefined: return Add(TreeLiteral{UndefinedLiteral{}}, origin);
          case analysis::SpecialValue::kNull: return Add(TreeLiteral{NullLiteral{}}, origin);
          case analysis::SpecialValue::kTrue: return Add(TreeLiteral{BooleanLiteral{true}}, origin);
          case analysis::SpecialValue::kFalse: return Add(TreeLiteral{BooleanLiteral{false}}, origin);
          case analysis::SpecialValue::kNaN:
            return Add(TreeLiteral{NumberLiteral{std::bit_cast<std::uint64_t>(
                std::numeric_limits<double>::quiet_NaN())}}, origin);
          case analysis::SpecialValue::kInfinity:
            return Add(TreeLiteral{NumberLiteral{std::bit_cast<std::uint64_t>(
                std::numeric_limits<double>::infinity())}}, origin);
        }
        throw Error::kUnsupportedOperation;
      }
    }, ResolveEvaluationOperand(evaluation_, operand));
  }
  void Stage(const PhiBundle& bundle, ThrowCopies placement, TreeOriginId origin) {
    const auto first = static_cast<std::uint32_t>(forest_.throw_assignments.size());
    for (std::size_t i = bundle.first; i < std::size_t(bundle.first) + bundle.count; ++i) {
      Step();
      if(evaluation_.exceptions->projection==ExceptionCopyProjection::kForOfClose && evaluation_.phis->projection[i]==PhiCopyUse::kForOfProtocol)continue;
      const auto slot = evaluation_.exceptions->staging[i];
      Require(slot.has_value(), Error::kBinding);
      const auto source = Operand(evaluation_.phis->copies[i].source, origin);
      Push(forest_.throw_assignments, Add(AssignmentExpr{SymbolTarget{SlotSymbol(*slot)}, source}, origin));
    }
    Push(forest_.throws, ThrowExpressions{placement, first,
        static_cast<std::uint32_t>(forest_.throw_assignments.size())-first});
  }
  void Commit(const CatchCommit& commit) {
    Step();
    Require(commit.block.value < graph_.blocks.size() &&
        graph_.phases[commit.block.value].phase == analysis::ExceptionPhase::kCatch, Error::kBinding);
    const auto& block = graph_.blocks[commit.block.value];
    Require(block.edge_count == 1, Error::kOrigin);
    // Catch has no instruction PC. Its only outgoing edge is the synthetic
    // binding/commit boundary; the handler's first instruction keeps its own PC.
    const auto origin = Origin(EdgeOrigin{block.first_edge});
    const auto first = static_cast<std::uint32_t>(forest_.catch_assignments.size());
    for (std::uint32_t i = 0; i < commit.count; ++i) {
      Step();
      const CatchSlotId slot{commit.first.value + i};
      const auto& copy = evaluation_.exceptions->slots[slot.value];
      const auto source = Add(SymbolExpr{SlotSymbol(slot)}, origin);
      Push(forest_.catch_assignments, Add(AssignmentExpr{SymbolTarget{PhiSymbol(copy.destination)}, source}, origin));
    }
    Push(forest_.catches, CatchExpressions{commit.block, first, commit.count});
  }
  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  ExpressionForest& forest_;
  FunctionOwnerId owner_;
  std::uint64_t work_;
  std::span<const std::uint8_t> selected_edges_;
};
}  // namespace
std::variant<std::uint64_t, ExpressionFailure> AppendCopyExpressions(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    ExpressionForest& forest, FunctionOwnerId owner, std::uint64_t work, std::span<const std::uint8_t> selected_edges) {
  try {
    CopyExpressions builder(graph, evaluation, forest, owner, work, selected_edges);
    builder.Append();
    return work - builder.Remaining();
  } catch (Error error) { return error; }
}
}  // namespace abcd::source
