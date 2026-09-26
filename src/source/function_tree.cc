#include "function_tree.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/source/global_reference.h"
#include "tree_semantics.h"

namespace abcd::source {
namespace {
using Error = FunctionTreeError;
void Require(bool condition, Error error) { if (!condition) throw error; }
struct ChildLink { StmtId statement; std::optional<std::size_t> next; };
struct BlockDraft {
  StmtId statement;
  ScopeId scope;
  std::optional<std::size_t> first;
  std::optional<std::size_t> last;
  std::uint32_t count = 0;
};
struct RegionTask { ControlId control; std::size_t draft; };
constexpr ScopeId kParameterScope{0};
constexpr ScopeId kBodyScope{1};
constexpr std::size_t kFunctionScopes = 2;
constexpr std::size_t kBranchArms = 2;

class TreeBuilder {
 public:
  TreeBuilder(const analysis::ValueProgram& graph, const ControlPlan& control,
      const EvaluationPlan& evaluation, FunctionSyntax syntax,
      ExpressionConstants constants, analysis::SemanticBudget budget)
      : graph_(graph), control_(control), evaluation_(evaluation),
        syntax_(std::move(syntax)), constants_(constants), budget_(budget),
        bytes_(budget.bytes), work_(budget.work) {}

  ConstructedFunction Build() {
    Require(syntax_.parameters.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Require(constants_.name_count == syntax_.names.size() &&
            constants_.text_count == syntax_.texts.size(), Error::kName);
    if (constants_.number_type_text) {
      Require(constants_.number_type_text->value < syntax_.texts.size() &&
              syntax_.texts[constants_.number_type_text->value] == u"number", Error::kName);
    }
    Admit(1, sizeof(TreeBuilder));
    Admit(syntax_.parameters.capacity(), sizeof(ParameterSyntax));
    Admit(syntax_.names.capacity(), sizeof(std::u16string));
    Admit(syntax_.texts.capacity(), sizeof(std::u16string));
    for (const auto& name : syntax_.names) { Step(); AdmitText(name); }
    for (const auto& text : syntax_.texts) { Step(); AdmitText(text); }
    const auto control = VerifyControlPlan(graph_, control_, {bytes_, work_});
    if (const auto* error = std::get_if<ControlError>(&control)) {
      if (*error == ControlError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == ControlError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kControl;
    }
    Step(std::get<std::uint64_t>(control));
    auto expressions = BuildExpressionForest(graph_, evaluation_,
        static_cast<std::uint32_t>(syntax_.parameters.size()), syntax_.owner,
        constants_, {bytes_, work_});
    if (const auto* error = std::get_if<ExpressionError>(&expressions)) {
      if (*error == ExpressionError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == ExpressionError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kExpression;
    }
    forest_ = std::get<ExpressionForest>(std::move(expressions));
    Admit(forest_.allocated_bytes, 1);
    Step(forest_.work);
    tree_.identity = syntax_.identity;
    tree_.method = syntax_.method;
    tree_.owner = syntax_.owner;
    tree_.callable = syntax_.callable;
    tree_.strictness = syntax_.strictness;
    tree_.value_revision = graph_.revision;
    tree_.cfg_revision = graph_.cfg_revision;
    tree_.names = std::move(syntax_.names);
    tree_.texts = std::move(syntax_.texts);
    tree_.expressions = std::move(forest_.expressions);
    tree_.properties = std::move(forest_.properties);
    tree_.arguments = std::move(forest_.arguments);
    tree_.origins = std::move(forest_.origins);
    tree_.origin_parts = std::move(forest_.origin_parts);
    Reserve();
    Bindings();
    Require(control_.entry.value < control_.nodes.size(), Error::kControl);
    const auto root_origin = BodyOrigin();
    const auto root = Block(kBodyScope, root_origin);
    tree_.body = drafts_[root].statement;
    for (std::size_t i = syntax_.parameters.size(); i < tree_.symbols.size(); ++i) {
      Step();
      if (tree_.symbols[i].kind == BindingKind::kLocal) continue;
      const SymbolId symbol{static_cast<std::uint32_t>(i)};
      const TreeOriginId origin{static_cast<std::uint32_t>(tree_.origins.size())};
      tree_.origins.emplace_back(BindingOrigin{symbol});
      Append(root, Statement(DeclarationStmt{symbol, {}}, origin));
    }
    // Formal defaults execute before the body. MethodEntry copies execute once
    // after private declarations, outside all loops and before original ops.
    if (entry_range_) EmitCopies(*entry_range_, root);
    tasks_.push_back({control_.entry, root});
    while (!tasks_.empty()) {
      Step();
      const auto task = tasks_.back();
      tasks_.pop_back();
      std::optional<ControlId> next = task.control;
      while (next) {
        Step();
        const auto& region = control_.nodes[next->value];
        if (std::holds_alternative<std::monostate>(region.synthetic)) EmitBlock(region, task.draft);
        else EmitLoop(region, task.draft);
        next = region.next;
      }
    }
    Flatten();
    CheckSemantics();
    return {std::move(tree_), budget_.bytes - bytes_, budget_.work - work_};
  }

 private:
  void Step(std::uint64_t count = 1) {
    Require(count <= work_, Error::kWorkLimit); work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit); bytes_ -= count * width;
  }
  void AdmitText(const std::u16string& text) {
    Admit(text.capacity(), sizeof(char16_t)); Admit(1, sizeof(char16_t));
  }
  template <class T> void Grow(std::vector<T>& values, std::size_t count) {
    if (count <= values.capacity()) return;
    // Reserve may temporarily retain the old allocation as well as the new.
    // Conservatively keep that old allocation charged for the rest of this pass.
    Admit(count, sizeof(T));
    values.reserve(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  void Reserve() {
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(control_.nodes.size() <= (kMaxId - kFunctionScopes) / kBranchArms,
            Error::kResourceLimit);
    const auto blocks = control_.nodes.size() * kBranchArms + 1;
    const auto synthetic = control_.nodes.size() - graph_.blocks.size();
    const auto symbols = forest_.bindings.symbols.size();
    Require(symbols < kMaxId && graph_.ops.size() <= kMaxId - blocks &&
            symbols <= kMaxId - blocks - graph_.ops.size(), Error::kResourceLimit);
    const auto original_statements = graph_.ops.size() + blocks + symbols;
    Require(synthetic <= kMaxId - original_statements &&
        forest_.edge_assignments.size() <= kMaxId - original_statements - synthetic, Error::kResourceLimit);
    const auto statements = original_statements + synthetic + forest_.edge_assignments.size();
    Require(tree_.names.size() <= kMaxId - symbols &&
            tree_.origins.size() <= kMaxId - symbols &&
            synthetic <= kMaxId - symbols - tree_.origins.size() &&
            tree_.expressions.size() <= kMaxId - symbols, Error::kResourceLimit);
    Grow(tree_.names, tree_.names.size() + symbols);
    Grow(tree_.origins, tree_.origins.size() + symbols + synthetic);
    Grow(tree_.expressions, tree_.expressions.size() + symbols);
    Grow(tree_.symbols, symbols);
    Grow(tree_.parameters, syntax_.parameters.size());
    Grow(tree_.scopes, blocks + 1);
    Grow(tree_.statements, statements);
    Grow(tree_.children, statements);
    Grow(tree_.anchors, graph_.ops.size());
    Grow(tree_.operations, graph_.ops.size());
    Grow(drafts_, blocks);
    Grow(links_, statements);
    Grow(tasks_, blocks);
    Grow(name_order_, syntax_.parameters.size());
    Grow(loops_, control_.loops.size());
    Step(control_.loops.size());
    loops_.resize(control_.loops.size());
    if (!forest_.edges.empty()) {
      Grow(edge_ranges_, graph_.edges.size());
      Step(graph_.edges.size());
      edge_ranges_.resize(graph_.edges.size());
      for (std::size_t i = 0; i < forest_.edges.size(); ++i) {
        Step();
        const auto* incoming = std::get_if<analysis::EdgeId>(&forest_.edges[i].incoming);
        if (!incoming) {
          Require(!entry_range_, Error::kControl);
          entry_range_ = i;
          continue;
        }
        const auto edge = *incoming;
        Require(edge.value < edge_ranges_.size() && !edge_ranges_[edge.value], Error::kControl);
        edge_ranges_[edge.value] = i;
      }
    }
  }
  NameId SyntheticName(std::u16string_view prefix, std::size_t index) {
    // Debug names use the existing conservative ASCII boundary, which excludes
    // '$'. Synthesized formals and snapshots therefore cannot shadow them.
    constexpr auto kMaxDigits = std::numeric_limits<std::size_t>::digits10 + 1;
    std::array<char, kMaxDigits> digits{};
    const auto converted = std::to_chars(digits.data(), digits.data() + digits.size(), index);
    Require(converted.ec == std::errc{}, Error::kName);
    const std::string_view suffix{digits.data(), static_cast<std::size_t>(converted.ptr - digits.data())};
    Step(prefix.size() + suffix.size());
    std::u16string text;
    Admit(prefix.size() + suffix.size() + 1, sizeof(char16_t));
    text.reserve(prefix.size() + suffix.size());
    Admit(text.capacity() - prefix.size() - suffix.size(), sizeof(char16_t));
    text.append(prefix);
    for (const auto ch : suffix) text.push_back(static_cast<char16_t>(ch));
    const NameId name{static_cast<std::uint32_t>(tree_.names.size())};
    tree_.names.push_back(std::move(text));
    return name;
  }
  void Bindings() {
    tree_.parameter_scope = kParameterScope;
    tree_.body_scope = kBodyScope;
    tree_.scopes.push_back({ScopeKind::kParameters, {}});
    tree_.scopes.push_back({ScopeKind::kFunctionBody, kParameterScope});
    for (std::size_t i = 0; i < syntax_.parameters.size(); ++i) {
      Step();
      const auto& input = syntax_.parameters[i];
      NameId name;
      if (input.name) {
        Require(input.name->value < constants_.name_count, Error::kName);
        const auto& text = tree_.names[input.name->value];
        Step(text.size());
        Require(input.evidence != NameEvidence::kSynthesized && AllowsGlobalReference(text, {}), Error::kName);
        name = *input.name;
      } else {
        Require(input.evidence == NameEvidence::kSynthesized, Error::kName);
        name = SyntheticName(u"$arg", i);
      }
      name_order_.push_back(name);
      const SymbolId symbol{static_cast<std::uint32_t>(i)};
      tree_.symbols.push_back({kParameterScope, name, BindingKind::kParameter,
          DeclarationKind::kParameter, input.evidence, {}});
      tree_.parameters.push_back({symbol, {}, false});
    }
    const auto less = [&](NameId left, NameId right) {
      const auto& a = tree_.names[left.value];
      const auto& b = tree_.names[right.value];
      Step(std::min(a.size(), b.size()) + 1);
      return a < b;
    };
    std::sort(name_order_.begin(), name_order_.end(), less);
    for (std::size_t i = 1; i < name_order_.size(); ++i)
      Require(less(name_order_[i - 1], name_order_[i]), Error::kName);
    for (std::size_t i = syntax_.parameters.size(); i < forest_.bindings.symbols.size(); ++i) {
      Step();
      const auto& binding = forest_.bindings.symbols[i];
      if (std::holds_alternative<CellBinding>(binding)) {
        // Final scope is the actual allocation container, never a hoisted
        // function-wide approximation of a possibly per-iteration cell.
        tree_.symbols.push_back({kBodyScope, SyntheticName(u"$local", i),
            BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
        continue;
      }
      TemporaryReason reason;
      std::u16string_view prefix;
      if (const auto* snapshot = std::get_if<SnapshotBinding>(&binding)) {
        reason = snapshot->reason; prefix = u"$value";
      } else if (std::holds_alternative<PhiBinding>(binding)) {
        reason = TemporaryReason::kPhiJoin; prefix = u"$phi";
      } else if (std::holds_alternative<CycleBinding>(binding)) {
        reason = TemporaryReason::kParallelCopyCycle; prefix = u"$copy";
      } else throw Error::kBinding;
      tree_.symbols.push_back({kBodyScope, SyntheticName(prefix, i),
          BindingKind::kCompilerTemporary, DeclarationKind::kLet,
          NameEvidence::kSynthesized, reason});
    }
  }
  TreeOriginId BodyOrigin() {
    // Removed entry copies/default prologues do not own the body braces.
    // Follow only the straight entry prefix; a live branch is itself a body
    // operation, so this never chooses one arm's source location for both.
    std::optional<ControlId> cursor = control_.entry;
    while (cursor) {
      Step();
      const auto& node = control_.nodes[cursor->value];
      if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) {
        cursor = control_.loops[loop->loop.value].body;
        continue;
      }
      Require(std::holds_alternative<std::monostate>(node.synthetic), Error::kOrigin);
      const auto& range = graph_.blocks[node.block.value];
      for (std::uint32_t i = 0; i < range.op_count; ++i) {
        Step();
        const auto& op = graph_.ops[range.first_op.value + i];
        if (op.life != analysis::ValueLife::kLive) continue;
        Require(op.origin.value < tree_.origins.size(), Error::kOrigin);
        return {op.origin.value};
      }
      Require(!node.arms, Error::kControl);
      cursor = node.next;
    }
    throw Error::kOrigin;
  }
  StmtId Statement(TreeStatement statement, TreeOriginId origin) {
    Step();
    Require(tree_.statements.size() < tree_.statements.capacity(), Error::kResourceLimit);
    const StmtId id{static_cast<std::uint32_t>(tree_.statements.size())};
    tree_.statements.push_back({std::move(statement), origin});
    return id;
  }
  std::size_t Block(ScopeId scope, TreeOriginId origin) {
    const auto id = Statement(BlockStmt{scope, 0, 0}, origin);
    Require(drafts_.size() < drafts_.capacity(), Error::kResourceLimit);
    drafts_.push_back({id, scope, {}, {}, 0});
    return drafts_.size() - 1;
  }
  std::size_t Arm(std::size_t parent, TreeOriginId origin,
                  std::optional<ControlId> first, analysis::EdgeId edge) {
    const ScopeId scope{static_cast<std::uint32_t>(tree_.scopes.size())};
    tree_.scopes.push_back({ScopeKind::kBlock, drafts_[parent].scope});
    const auto draft = Block(scope, origin);
    EmitEdge(edge, draft);
    if (first) tasks_.push_back({*first, draft});
    return draft;
  }
  bool HasCopies(analysis::EdgeId edge) const {
    return !edge_ranges_.empty() && edge_ranges_[edge.value] &&
        forest_.edges[*edge_ranges_[edge.value]].count != 0;
  }
  void EmitEdge(analysis::EdgeId edge, std::size_t draft) {
    Step();
    if (edge_ranges_.empty() || !edge_ranges_[edge.value]) return;
    EmitCopies(*edge_ranges_[edge.value], draft);
  }
  void EmitCopies(std::size_t range_id, std::size_t draft) {
    const auto& range = forest_.edges[range_id];
    for (std::uint32_t i = 0; i < range.count; ++i) {
      Step();
      const auto expression = forest_.edge_assignments[range.first + i];
      Append(draft, Statement(ExpressionStmt{expression}, tree_.expressions[expression.value].origin));
    }
  }
  void Append(std::size_t draft, StmtId statement) {
    Require(links_.size() < links_.capacity(), Error::kResourceLimit);
    auto& block = drafts_[draft];
    const auto next = links_.size();
    links_.push_back({statement, {}});
    if (block.last) links_[*block.last].next = next;
    else block.first = next;
    block.last = next;
    ++block.count;
  }
  void EmitBlock(const ControlNode& region, std::size_t draft) {
    const auto& block = graph_.blocks[region.block.value];
    for (std::uint32_t j = 0; j < block.op_count; ++j) {
      Step();
      const analysis::OpId id{block.first_op.value + j};
      const auto& op = graph_.ops[id.value];
      const auto& evaluation = evaluation_.operations[id.value];
      if (op.life != analysis::ValueLife::kLive) continue;
      const TreeOriginId origin{op.origin.value};
      const auto expression = forest_.operations[id.value];
      if (evaluation.kind == EvaluationKind::kEnvironment) {
        const AnchorId anchor{static_cast<std::uint32_t>(tree_.anchors.size())};
        tree_.anchors.push_back({BlockBoundary{drafts_[draft].statement, drafts_[draft].count}, origin});
        tree_.operations.push_back({id, anchor});
        EmitCells(id, draft, origin);
        continue;
      }
      if (evaluation.kind == EvaluationKind::kInitializer) {
        Require(forest_.initializers[id.value].has_value(), Error::kOwnership);
        tree_.operations.push_back({id, *forest_.initializers[id.value]});
        continue;
      }
      if (std::holds_alternative<analysis::ValueJump>(op.operation)) {
        const AnchorId anchor{static_cast<std::uint32_t>(tree_.anchors.size())};
        tree_.anchors.push_back({BlockBoundary{drafts_[draft].statement, drafts_[draft].count}, origin});
        tree_.operations.push_back({id, anchor});
        continue;
      }
      Require(expression.has_value(), Error::kOwnership);
      if (std::holds_alternative<analysis::ValueBranch>(op.operation)) {
        Require(region.arms.has_value(), Error::kControl);
        const auto taken = Arm(draft, origin, region.arms->taken, block.first_edge);
        const analysis::EdgeId fallthrough{block.first_edge.value + 1};
        std::optional<StmtId> alternate;
        if (region.arms->fallthrough || HasCopies(fallthrough))
          alternate = drafts_[Arm(draft, origin, region.arms->fallthrough, fallthrough)].statement;
        const auto statement = Statement(IfStmt{*expression, drafts_[taken].statement, alternate}, origin);
        Append(draft, statement);
        tree_.operations.push_back({id, statement});
      } else if (std::holds_alternative<analysis::ValueReturn>(op.operation)) {
        const auto statement = Statement(ReturnStmt{expression}, origin);
        Append(draft, statement);
        tree_.operations.push_back({id, statement});
      } else {
        tree_.operations.push_back({id, *expression});
        if (evaluation.kind == EvaluationKind::kInline) continue;
        auto evaluated = *expression;
        if (evaluation.temporary) {
          Require(op.result.has_value(), Error::kBinding);
          const auto* symbol = std::get_if<SymbolId>(&forest_.bindings.values[op.result->value]);
          Require(symbol != nullptr, Error::kBinding);
          evaluated = ExprId{static_cast<std::uint32_t>(tree_.expressions.size())};
          tree_.expressions.push_back({AssignmentExpr{SymbolTarget{*symbol}, *expression}, origin});
        }
        Append(draft, Statement(ExpressionStmt{evaluated}, origin));
      }
    }
    // A straight edge executes after the source block's operations and before
    // its successor. Branch copies belong inside their selected arm instead.
    if (!region.arms && block.edge_count == 1) EmitEdge(block.first_edge, draft);
  }
  void EmitCells(analysis::OpId allocation, std::size_t draft, TreeOriginId origin) {
    Require(evaluation_.cells.has_value(), Error::kBinding);
    if (!std::holds_alternative<analysis::ValueEnvironmentPush>(graph_.ops[allocation.value].operation)) return;
    const auto& cells = evaluation_.cells->cells.cells;
    // Verified CellPlan storage is ordered by allocation then slot. A binary
    // search avoids rescanning every cell for each environment operation.
    auto cell = std::lower_bound(cells.begin(), cells.end(), allocation,
        [&](const Cell& item, analysis::OpId site) {
          Step();
          const auto* local = std::get_if<AllocatedCell>(&item.storage);
          Require(local != nullptr, Error::kBinding);
          return local->allocation < site;
        });
    for (; cell != cells.end(); ++cell) {
      Step();
      const auto* local = std::get_if<AllocatedCell>(&cell->storage);
      Require(local != nullptr, Error::kBinding);
      if (local->allocation != allocation) break;
      const auto symbol = forest_.bindings.cells[static_cast<std::size_t>(cell - cells.begin())];
      tree_.symbols[symbol.value].scope = drafts_[draft].scope;
      Append(draft, Statement(DeclarationStmt{symbol, {}}, origin));
    }
  }
  void EmitLoop(const ControlNode& region, std::size_t draft) {
    const TreeOriginId origin{static_cast<std::uint32_t>(tree_.origins.size())};
    Require(tree_.origins.size() < tree_.origins.capacity(), Error::kResourceLimit);
    if (const auto* wrapper = std::get_if<LoopControl>(&region.synthetic)) {
      const auto& loop = control_.loops[wrapper->loop.value];
      tree_.origins.emplace_back(LoopOrigin{loop.header});
      const ScopeId scope{static_cast<std::uint32_t>(tree_.scopes.size())};
      tree_.scopes.push_back({ScopeKind::kBlock, drafts_[draft].scope});
      const auto body = Block(scope, origin);
      const auto statement = Statement(LoopStmt{drafts_[body].statement}, origin);
      Require(!loops_[wrapper->loop.value], Error::kControl);
      loops_[wrapper->loop.value] = statement;
      Append(draft, statement);
      tasks_.push_back({loop.body, body});
      return;
    }
    const auto& transfer = std::get<LoopTransfer>(region.synthetic);
    Require(loops_[transfer.loop.value].has_value(), Error::kControl);
    if (const auto* edge = std::get_if<analysis::EdgeId>(&transfer.source))
      tree_.origins.emplace_back(EdgeOrigin{*edge});
    else tree_.origins.emplace_back(LoopOrigin{control_.loops[std::get<LoopId>(transfer.source).value].header});
    const auto target = *loops_[transfer.loop.value];
    switch (transfer.kind) {
      case LoopTransferKind::kBreak: Append(draft, Statement(BreakStmt{target}, origin)); break;
      case LoopTransferKind::kContinue: Append(draft, Statement(ContinueStmt{target}, origin)); break;
    }
  }
  void Flatten() {
    for (const auto& draft : drafts_) {
      Step();
      auto& block = std::get<BlockStmt>(tree_.statements[draft.statement.value].statement);
      block.first_child = static_cast<std::uint32_t>(tree_.children.size());
      block.child_count = draft.count;
      auto child = draft.first;
      while (child) {
        Step();
        tree_.children.push_back(links_[*child].statement);
        child = links_[*child].next;
      }
    }
  }
  void CheckSemantics() {
    // The checker admits tree storage too. Recredit only that already-admitted
    // output, not builder scratch; both remain live throughout verification.
    const auto measured = MeasureTreeStorage(tree_, {budget_.bytes, work_});
    const auto* storage = std::get_if<TreeStorage>(&measured);
    if (!storage)
      throw std::get<TreeStorageError>(measured) == TreeStorageError::kWorkLimit
          ? Error::kWorkLimit : Error::kResourceLimit;
    Step(storage->work);
    Require(storage->bytes <= budget_.bytes - bytes_, Error::kResourceLimit);
    const auto checked = VerifyTreeSemantics(graph_, control_, evaluation_, tree_, constants_,
                                             {bytes_ + storage->bytes, work_});
    const auto* semantics = std::get_if<TreeSemantics>(&checked);
    if (!semantics) {
      const auto error = std::get<TreeSemanticError>(checked);
      if (error == TreeSemanticError::kWorkLimit) throw Error::kWorkLimit;
      if (error == TreeSemanticError::kResourceLimit) throw Error::kResourceLimit;
      throw Error::kSemantics;
    }
    Require(semantics->allocated_bytes >= storage->bytes, Error::kSemantics);
    Admit(semantics->allocated_bytes - storage->bytes, 1);
    Step(semantics->work);
  }

  const analysis::ValueProgram& graph_;
  const ControlPlan& control_;
  const EvaluationPlan& evaluation_;
  FunctionSyntax syntax_;
  ExpressionConstants constants_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  ExpressionForest forest_;
  FunctionTree tree_;
  std::vector<BlockDraft> drafts_;
  std::vector<ChildLink> links_;
  std::vector<RegionTask> tasks_;
  std::vector<NameId> name_order_;
  std::vector<std::optional<std::size_t>> edge_ranges_;
  std::vector<std::optional<StmtId>> loops_;
  std::optional<std::size_t> entry_range_;
};
}  // namespace

std::variant<ConstructedFunction, FunctionTreeError> BuildFunctionTree(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, FunctionSyntax syntax,
    ExpressionConstants constants, analysis::SemanticBudget budget) {
  try {
    return TreeBuilder(graph, control, evaluation, std::move(syntax), constants, budget).Build();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
