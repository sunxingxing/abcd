#include "function_tree.h"
#include "exception_tree.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/source/global_reference.h"
#include "abcd/source/scalar_expression.h"
#include "tree_semantics.h"
#include "capture_aliases.h"
#include "cell_lifetime.h"
#include "tree_structure.h"

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
constexpr ScopeId kCaptureScope{2};
constexpr std::size_t kFunctionScopes = 2;
constexpr std::size_t kBranchArms = 2;

class TreeBuilder {
 public:
  TreeBuilder(const analysis::ValueProgram& graph, const ControlPlan& control,
      const EvaluationPlan& evaluation, FunctionSyntax syntax,
      ExpressionConstants constants, analysis::SemanticBudget budget,
      const IncomingCaptureTree* incoming, const ExceptionTreeInput* exceptions = nullptr,
      RecoveryMode recovery = RecoveryMode::kCompleteOnly)
      : graph_(graph), control_(control), evaluation_(evaluation),
        syntax_(std::move(syntax)), constants_(constants), budget_(budget),
        bytes_(budget.bytes), work_(budget.work), incoming_(incoming), exceptions_(exceptions), recovery_(recovery) {}

  ConstructedFunction Build() {
    if (exceptions_ && evaluation_.cells) CheckCellLifetimes();
    Require(syntax_.parameters.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Require(constants_.name_count == syntax_.names.size() &&
            constants_.text_count == syntax_.texts.size(), Error::kName);
    if (constants_.number_type_text) {
      Require(constants_.number_type_text->value < syntax_.texts.size() &&
              syntax_.texts[constants_.number_type_text->value] == u"number", Error::kName);
    }
    Admit(1, sizeof(TreeBuilder));
    Admit(syntax_.parameters.capacity(), sizeof(ParameterSyntax));
    Admit(syntax_.captures.capacity(), sizeof(CaptureSyntax));
    Admit(syntax_.modules.capacity(), sizeof(ModuleSyntax));
    Admit(syntax_.names.capacity(), sizeof(std::u16string));
    Admit(syntax_.texts.capacity(), sizeof(std::u16string));
    for (const auto& name : syntax_.names) { Step(); AdmitText(name); }
    for (const auto& text : syntax_.texts) { Step(); AdmitText(text); }
    CaptureNames();
    if (exceptions_) {
      const auto checked = VerifyExceptionControl(exceptions_->body, graph_, exceptions_->input,
          exceptions_->regions, exceptions_->control, {bytes_, work_});
      if (const auto* error = std::get_if<ExceptionControlError>(&checked)) {
        if (*error == ExceptionControlError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == ExceptionControlError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      Step(std::get<std::uint64_t>(checked));
    } else {
      const auto control = VerifyControlPlan(graph_, control_, {bytes_, work_});
      if (const auto* error = std::get_if<ControlError>(&control)) {
        if (*error == ControlError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == ControlError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      Step(std::get<std::uint64_t>(control));
    }
    std::vector<ModuleSymbol> modules;
    Grow(modules, syntax_.modules.size());
    for (const auto& alias : syntax_.modules) { Step(); modules.push_back(alias.target); }
    const auto build_forest = exceptions_ ? BuildPhasedExpressionForest : BuildExpressionForest;
    auto expressions = build_forest(graph_, evaluation_,
        static_cast<std::uint32_t>(syntax_.parameters.size()), syntax_.owner,
        constants_, {bytes_, work_}, modules,
        (incoming_ && !incoming_->cells.targets.empty()) || exceptions_ || !syntax_.captures.empty()
            ? RecoveryMode::kCompleteOnly : recovery_);
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
    tree_.inferred_name = syntax_.inferred_name;
    tree_.value_revision = graph_.revision;
    tree_.cfg_revision = graph_.cfg_revision;
    tree_.names = std::move(syntax_.names);
    tree_.texts = std::move(syntax_.texts);
    tree_.expressions = std::move(forest_.expressions);
    if (tree_.callable == CallableKind::kArrow) {
      for (const auto& node : tree_.expressions) {
        Step();
        if (std::holds_alternative<ArgumentSnapshotExpr>(node.expression))
          tree_.invocation = InvocationForm::kArrowMethod;
      }
    }
    tree_.properties = std::move(forest_.properties);
    tree_.arguments = std::move(forest_.arguments);
    tree_.closure_captures = std::move(forest_.closure_captures);
    tree_.origins = std::move(forest_.origins);
    tree_.origin_parts = std::move(forest_.origin_parts);
    Reserve();
    ReservedNames();
    Bindings();
    Require(control_.entry.value < control_.nodes.size(), Error::kControl);
    const auto root_origin = BodyOrigin();
    const auto root = Block(kBodyScope, root_origin);
    tree_.body = drafts_[root].statement;
    for (std::size_t i = syntax_.parameters.size(); i < tree_.symbols.size(); ++i) {
      Step();
      if (tree_.symbols[i].kind == BindingKind::kLocal || tree_.symbols[i].kind == BindingKind::kCapture ||
          tree_.symbols[i].kind == BindingKind::kModule || tree_.symbols[i].kind == BindingKind::kFunctionSelf ||
          tree_.symbols[i].kind == BindingKind::kCatchParameter) continue;
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
        if (!std::holds_alternative<std::monostate>(region.synthetic)) EmitLoop(region, task.draft);
        else if (exceptions_) EmitExceptionUnit(*next, task.draft);
        else EmitBlock(region, task.draft);
        next = region.next;
      }
    }
    Flatten();
    if (forest_.recovery_stop) {
      const auto checked = VerifyTreeStructure(tree_, {bytes_, work_});
      if (const auto* error = std::get_if<TreeStructureError>(&checked)) {
        if (*error == TreeStructureError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == TreeStructureError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kStructure;
      }
    } else CheckSemantics();
    return {std::move(tree_), budget_.bytes - bytes_, budget_.work - work_};
  }

 private:
  void CheckCellLifetimes() {
    auto environments = analysis::BuildPhasedValueEnvironments(graph_, {bytes_, work_});
    if (const auto* error = std::get_if<analysis::ValueEnvironmentError>(&environments)) {
      if (*error == analysis::ValueEnvironmentError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == analysis::ValueEnvironmentError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kBinding;
    }
    const auto& states = std::get<analysis::ValueEnvironments>(environments);
    Admit(states.allocated_bytes, 1); Step(states.work);
    const auto lifetimes = BuildPhasedCellLifetimes(graph_, states, evaluation_.cells->cells,
        *exceptions_, {bytes_, work_});
    if (const auto* error = std::get_if<CellLifetimeError>(&lifetimes)) {
      if (*error == CellLifetimeError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == CellLifetimeError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kBinding;
    }
    const auto& scopes = std::get<CellLifetimes>(lifetimes);
    Admit(scopes.allocated_bytes, 1); Step(scopes.work);
    // This candidate never authorizes hoisting. Tree semantics independently
    // checks allocation origins, declaration placement and actual use scopes.
  }
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
    const auto capture_scopes = syntax_.captures.empty() ? 0U : 1U;
    const auto module_scopes = syntax_.modules.empty() ? 0U : 1U;
    const auto self_scopes = forest_.bindings.self ? 1U : 0U;
    Require(control_.nodes.size() <= (kMaxId - kFunctionScopes - capture_scopes - module_scopes - self_scopes) / kBranchArms,
            Error::kResourceLimit);
    const auto blocks = control_.nodes.size() * kBranchArms + 1;
    const auto synthetic = control_.nodes.size() - graph_.blocks.size();
    Require(forest_.bindings.symbols.size() <= kMaxId &&
        syntax_.modules.size() <= kMaxId - forest_.bindings.symbols.size(), Error::kResourceLimit);
    const auto symbols = forest_.bindings.symbols.size() + syntax_.modules.size();
    Require(symbols < kMaxId && graph_.ops.size() <= kMaxId - blocks &&
            symbols <= kMaxId - blocks - graph_.ops.size(), Error::kResourceLimit);
    const auto original_statements = graph_.ops.size() + blocks + symbols;
    Require(synthetic <= kMaxId - original_statements &&
        forest_.edge_assignments.size() <= kMaxId - original_statements - synthetic, Error::kResourceLimit);
    auto statements = original_statements + synthetic + forest_.edge_assignments.size();
    Require(forest_.throw_assignments.size() <= kMaxId - statements, Error::kResourceLimit);
    statements += forest_.throw_assignments.size();
    Require(forest_.catch_assignments.size() <= kMaxId - statements, Error::kResourceLimit);
    statements += forest_.catch_assignments.size();
    Require(tree_.names.size() <= kMaxId - symbols &&
            tree_.origins.size() <= kMaxId - symbols &&
            synthetic <= kMaxId - symbols - tree_.origins.size() &&
            tree_.expressions.size() <= kMaxId - symbols, Error::kResourceLimit);
    Grow(tree_.names, tree_.names.size() + symbols);
    Grow(tree_.origins, tree_.origins.size() + symbols + synthetic);
    Grow(tree_.expressions, tree_.expressions.size() + symbols);
    Grow(tree_.symbols, symbols);
    Grow(tree_.parameters, syntax_.parameters.size());
    Grow(tree_.captures, syntax_.captures.size());
    Grow(tree_.modules, syntax_.modules.size());
    Grow(tree_.scopes, blocks + 1 + capture_scopes + module_scopes + self_scopes);
    Grow(tree_.statements, statements);
    Grow(tree_.children, statements);
    Grow(tree_.anchors, graph_.ops.size());
    Grow(tree_.operations, graph_.ops.size());
    Grow(drafts_, blocks);
    Grow(links_, statements);
    Grow(tasks_, blocks);
    Grow(name_order_, syntax_.parameters.size());
    Grow(reserved_names_, constants_.name_count);
    Grow(loops_, control_.loops.size());
    Step(control_.loops.size());
    loops_.resize(control_.loops.size());
    if (exceptions_) {
      Grow(catch_symbols_, graph_.blocks.size()); catch_symbols_.resize(graph_.blocks.size());
      Grow(catch_ranges_, graph_.blocks.size()); catch_ranges_.resize(graph_.blocks.size());
      Grow(throw_ranges_, graph_.ops.size()); throw_ranges_.resize(graph_.ops.size());
      Step(2 * graph_.blocks.size() + graph_.ops.size());
      for (std::size_t i = 0; i < forest_.catches.size(); ++i) {
        Step(); const auto block = forest_.catches[i].block.value;
        Require(block < catch_ranges_.size() && !catch_ranges_[block], Error::kControl);
        catch_ranges_[block] = i;
      }
      for (std::size_t i = 0; i < forest_.throws.size(); ++i) {
        Step(); const auto op = forest_.throws[i].placement.operation.value;
        Require(op < throw_ranges_.size() && !throw_ranges_[op], Error::kControl);
        throw_ranges_[op] = i;
      }
    }
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
  void ReservedNames() {
    // Includes incoming aliases and textual global references. A generated
    // local must not capture either merely because its preferred name matches.
    for (std::size_t i = 0; i < constants_.name_count; ++i) {
      Step(); reserved_names_.push_back(NameId{static_cast<std::uint32_t>(i)});
    }
    std::sort(reserved_names_.begin(), reserved_names_.end(), [&](NameId a, NameId b) {
      const auto& left = tree_.names[a.value];
      const auto& right = tree_.names[b.value];
      Step(std::min(left.size(), right.size()) + 1);
      return left < right;
    });
  }
  bool ReservedName(std::u16string_view text) {
    const auto found = std::lower_bound(reserved_names_.begin(), reserved_names_.end(), text,
        [&](NameId id, std::u16string_view candidate) {
          const auto& name = tree_.names[id.value];
          Step(std::min(name.size(), candidate.size()) + 1);
          return name < candidate;
        });
    if (found == reserved_names_.end()) return false;
    const auto& name = tree_.names[found->value];
    Step(std::min(name.size(), text.size()) + 1);
    return name == text;
  }
  std::u16string CandidateName(std::u16string_view prefix, std::size_t index) {
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
    return text;
  }
  NameId SyntheticName(std::u16string_view prefix, std::size_t index) {
    auto text = CandidateName(prefix, index);
    // Preferred namespaces ($arg/$local/$value/$phi/$copy) are disjoint and
    // indexed by unique symbols. The fallback namespace has one monotone
    // counter across the function, so retries total at most reserved names +
    // generated symbols, not a fresh linear search for every declaration.
    while (ReservedName(text)) {
      Step();
      Require(next_binding_name_ != std::numeric_limits<std::size_t>::max(), Error::kResourceLimit);
      text = CandidateName(u"$binding", next_binding_name_++);
    }
    const NameId name{static_cast<std::uint32_t>(tree_.names.size())};
    tree_.names.push_back(std::move(text));
    return name;
  }
  bool CanNameSelf(NameId candidate) {
    const auto& name = tree_.names[candidate.value];
    Step(name.size());
    if (tree_.callable != CallableKind::kFunction ||
        !IsSourceIdentifier(std::u16string_view{name})) return false;
    const auto conflicts = [&](NameId other) {
      Require(other.value < tree_.names.size(), Error::kName);
      const auto& text = tree_.names[other.value];
      Step(std::min(name.size(), text.size()) + 1);
      return name == text;
    };
    for (const auto& parameter : syntax_.parameters) {
      Step(); if (parameter.name && conflicts(*parameter.name)) return false;
    }
    for (const auto& capture : syntax_.captures) { Step(); if (conflicts(capture.name)) return false; }
    for (const auto& module : syntax_.modules) { Step(); if (conflicts(module.name)) return false; }
    // Check once per owner, not once per frame read. A private binding also
    // avoids introducing a source-name environment around descendant functions.
    for (const auto& node : tree_.expressions) {
      Step();
      if (std::holds_alternative<FunctionExpr>(node.expression)) return false;
      if (std::holds_alternative<RealmObjectExpr>(node.expression)) return false;
      if (std::holds_alternative<SymbolConstructorExpr>(node.expression)) return false;
      const auto* global = std::get_if<GlobalNameExpr>(&node.expression);
      if (global && conflicts(global->name)) return false;
    }
    return true;
  }
  void Bindings() {
    tree_.parameter_scope = kParameterScope;
    tree_.body_scope = kBodyScope;
    if (!syntax_.captures.empty()) tree_.capture_scope = kCaptureScope;
    if (!syntax_.modules.empty())
      tree_.module_scope = ScopeId{static_cast<std::uint32_t>(kFunctionScopes + (tree_.capture_scope ? 1U : 0U))};
    tree_.scopes.push_back({ScopeKind::kParameters, tree_.capture_scope ? tree_.capture_scope : tree_.module_scope});
    tree_.scopes.push_back({ScopeKind::kFunctionBody, kParameterScope});
    if (tree_.capture_scope) tree_.scopes.push_back({ScopeKind::kCaptures, tree_.module_scope});
    if (tree_.module_scope) tree_.scopes.push_back({ScopeKind::kModule, {}});
    std::optional<ScopeId> self_scope;
    if (forest_.bindings.self) {
      Require((tree_.callable == CallableKind::kFunction || tree_.callable == CallableKind::kArrow) && tree_.inferred_name &&
          tree_.inferred_name->value < tree_.names.size(), Error::kBinding);
      if (!CanNameSelf(*tree_.inferred_name)) tree_.self_form = SelfForm::kCreationBinding;
      if (tree_.self_form == SelfForm::kCreationBinding)
        Require(ResourceFailures(tree_.identity.contract) == ResourceFailureScope::kExcluded, Error::kBinding);
      tree_.self = forest_.bindings.self;
      self_scope = ScopeId{static_cast<std::uint32_t>(tree_.scopes.size())};
      tree_.scopes.push_back({ScopeKind::kFunctionSelf, tree_.scopes[kParameterScope.value].parent});
      tree_.scopes[kParameterScope.value].parent = self_scope;
    }
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
    std::size_t next_capture = 0;
    for (std::size_t i = syntax_.parameters.size(); i < forest_.bindings.symbols.size(); ++i) {
      Step();
      const auto& binding = forest_.bindings.symbols[i];
      if (const auto* caught = std::get_if<CatchParameterBinding>(&binding)) {
        Require(exceptions_ && caught->block.value < catch_symbols_.size() &&
            !catch_symbols_[caught->block.value], Error::kBinding);
        catch_symbols_[caught->block.value] = SymbolId{static_cast<std::uint32_t>(i)};
        // EmitTry assigns the actual handler scope before syntax verification.
        tree_.symbols.push_back({kBodyScope, SyntheticName(u"$caught", i), BindingKind::kCatchParameter,
            DeclarationKind::kCatchParameter, NameEvidence::kSynthesized, {}});
        continue;
      }
      if (std::holds_alternative<FunctionSelfBinding>(binding)) {
        Require(self_scope && tree_.self == SymbolId{static_cast<std::uint32_t>(i)}, Error::kBinding);
        const bool named = tree_.self_form == SelfForm::kNamedFunction;
        const auto name = named ? *tree_.inferred_name : SyntheticName(u"$self", i);
        tree_.symbols.push_back({*self_scope, name, BindingKind::kFunctionSelf,
            DeclarationKind::kConst, named ? NameEvidence::kMetadata : NameEvidence::kSynthesized, {}});
        continue;
      }
      if (const auto* cell = std::get_if<CellBinding>(&binding)) {
        Require(evaluation_.cells && cell->cell.value < evaluation_.cells->cells.cells.size(), Error::kBinding);
        if (std::holds_alternative<IncomingCell>(evaluation_.cells->cells.cells[cell->cell.value].storage)) {
          Require(evaluation_.cells->incoming == IncomingCells::kConditional &&
              next_capture < syntax_.captures.size(), Error::kBinding);
          const auto& capture = syntax_.captures[next_capture++];
          Require(capture.cell == cell->cell && capture.name.value < constants_.name_count &&
              capture.target.owner != tree_.owner && tree_.capture_scope, Error::kBinding);
          tree_.symbols.push_back({*tree_.capture_scope, capture.name, BindingKind::kCapture,
              DeclarationKind::kLet, capture.evidence, {}});
          tree_.captures.push_back({SymbolId{static_cast<std::uint32_t>(i)}, capture.target});
          continue;
        }
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
      } else if (std::holds_alternative<CatchSlotBinding>(binding)) {
        Require(exceptions_ != nullptr, Error::kBinding);
        reason = TemporaryReason::kExceptionSnapshot; prefix = u"$exception";
      } else throw Error::kBinding;
      tree_.symbols.push_back({kBodyScope, SyntheticName(prefix, i),
          BindingKind::kCompilerTemporary, DeclarationKind::kLet,
          NameEvidence::kSynthesized, reason});
    }
    Require(next_capture == syntax_.captures.size(), Error::kBinding);
    for (const auto& alias : syntax_.modules) {
      Step();
      Require(tree_.module_scope && alias.name.value < constants_.name_count, Error::kBinding);
      DeclarationKind declaration;
      switch (alias.access) {
        case ModuleAccess::kReadOnly: declaration = DeclarationKind::kConst; break;
        case ModuleAccess::kMutable: declaration = DeclarationKind::kLet; break;
        default: throw Error::kBinding;
      }
      const SymbolId symbol{static_cast<std::uint32_t>(tree_.symbols.size())};
      tree_.symbols.push_back({*tree_.module_scope, alias.name, BindingKind::kModule, declaration, alias.evidence, {}});
      tree_.modules.push_back({symbol, alias.target, alias.access});
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
      if (exceptions_ && std::holds_alternative<std::monostate>(node.synthetic)) {
        const auto& unit = exceptions_->control.units[cursor->value];
        if (const auto* region = std::get_if<TryControlUnit>(&unit)) {
          cursor = exceptions_->control.regions[region->region.value].body;
          continue;
        }
        Require(std::holds_alternative<PhaseControlUnit>(unit), Error::kOrigin);
      }
      if (const auto* loop = std::get_if<LoopControl>(&node.synthetic)) {
        cursor = control_.loops[loop->loop.value].body;
        continue;
      }
      Require(std::holds_alternative<std::monostate>(node.synthetic), Error::kOrigin);
      const auto& range = graph_.blocks[node.block.value];
      for (std::uint32_t i = 0; i < range.op_count; ++i) {
        Step();
        const auto& op = graph_.ops[range.first_op.value + i];
        if (op.life != analysis::ValueLife::kLive ||
            evaluation_.operations[range.first_op.value + i].kind == EvaluationKind::kElidedPure ||
            evaluation_.operations[range.first_op.value + i].kind == EvaluationKind::kAlias) continue;
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
      if (forest_.recovery_stop && id == *forest_.recovery_stop) {
        Grow(tree_.recovery_issues, 1);
        tree_.recovery_issues.push_back({RecoveryReason::kUnsupportedOperation,
            graph_.origins[op.origin.value].pc, {}});
        Append(draft, Statement(UnrecoveredStmt{{0}}, TreeOriginId{op.origin.value}));
        return;
      }
      const auto& evaluation = evaluation_.operations[id.value];
      if (op.life != analysis::ValueLife::kLive) continue;
      if (exceptions_ && std::holds_alternative<analysis::ValueBranch>(op.operation)) {
        // Branch successors belong to Commit. Its condition may be emitted
        // there only across an empty Before->Commit boundary, never phi writes.
        Require(evaluation.kind == EvaluationKind::kRoot && block.edge_count == 1 &&
            !HasCopies(block.first_edge), Error::kControl);
        continue;
      }
      if (exceptions_ && throw_ranges_[id.value]) {
        const auto& range = forest_.throws[*throw_ranges_[id.value]];
        EmitAssignments(forest_.throw_assignments, range.first, range.count, draft);
      }
      const TreeOriginId origin{op.origin.value};
      const auto expression = forest_.operations[id.value];
      if (evaluation.kind == EvaluationKind::kEnvironment || evaluation.kind == EvaluationKind::kElidedPure ||
          evaluation.kind == EvaluationKind::kAlias) {
        // A projected pure elision keeps its PC at this lexical boundary, not
        // on an unrelated expression or a fabricated executable statement.
        const AnchorId anchor{static_cast<std::uint32_t>(tree_.anchors.size())};
        tree_.anchors.push_back({BlockBoundary{drafts_[draft].statement, drafts_[draft].count}, origin});
        tree_.operations.push_back({id, anchor});
        if (evaluation.kind == EvaluationKind::kEnvironment) EmitCells(id, draft, origin);
        continue;
      }
      if (evaluation.kind == EvaluationKind::kInitializer) {
        Require(forest_.initializers[id.value].has_value(), Error::kOwnership);
        tree_.operations.push_back({id, *forest_.initializers[id.value]});
        continue;
      }
      if (evaluation.kind == EvaluationKind::kModuleGuard) {
        Require(expression.has_value(), Error::kOwnership);
        tree_.operations.push_back({id, *expression});
        continue;
      }
      if (std::holds_alternative<analysis::ValueJump>(op.operation)) {
        const AnchorId anchor{static_cast<std::uint32_t>(tree_.anchors.size())};
        tree_.anchors.push_back({BlockBoundary{drafts_[draft].statement, drafts_[draft].count}, origin});
        tree_.operations.push_back({id, anchor});
        continue;
      }
      if (std::holds_alternative<analysis::ValueDebugger>(op.operation)) {
        Require(!expression && !op.result, Error::kOwnership);
        const auto statement = Statement(DebuggerStmt{}, origin);
        Append(draft, statement);
        tree_.operations.push_back({id, statement});
        continue;
      }
      Require(expression.has_value(), Error::kOwnership);
      if (std::holds_alternative<analysis::ValueBranch>(op.operation)) {
        EmitBranch(id, region, block.first_edge, draft);
      } else if (std::holds_alternative<analysis::ValueReturn>(op.operation)) {
        const auto statement = Statement(ReturnStmt{expression}, origin);
        Append(draft, statement);
        tree_.operations.push_back({id, statement});
      } else if (std::holds_alternative<analysis::ValueThrow>(op.operation)) {
        const auto statement = Statement(ThrowStmt{*expression}, origin);
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
    if (!region.arms) {
      std::uint8_t normal = 0;
      for (std::uint8_t e = 0; e < block.edge_count; ++e) {
        Step(); const analysis::EdgeId edge{block.first_edge.value + e};
        if (exceptions_ && graph_.edges[edge.value].kind == analysis::FlowEdgeKind::kException) continue;
        Require(++normal == 1, Error::kControl);
        EmitEdge(edge, draft);
      }
    }
  }
  void EmitAssignments(const std::vector<ExprId>& roots, std::uint32_t first,
                       std::uint32_t count, std::size_t draft) {
    for (std::uint32_t i = 0; i < count; ++i) {
      Step(); const auto expression = roots[first + i];
      Append(draft, Statement(ExpressionStmt{expression}, tree_.expressions[expression.value].origin));
    }
  }
  void EmitBranch(analysis::OpId op, const ControlNode& node,
                  analysis::EdgeId first_edge, std::size_t draft) {
    Require(node.arms.has_value(), Error::kControl);
    const TreeOriginId origin{graph_.ops[op.value].origin.value};
    const auto expression = forest_.operations[op.value];
    Require(expression.has_value(), Error::kOwnership);
    const auto taken = Arm(draft, origin, node.arms->taken, first_edge);
    const analysis::EdgeId fallthrough{first_edge.value + 1};
    std::optional<StmtId> alternate;
    if (node.arms->fallthrough || HasCopies(fallthrough))
      alternate = drafts_[Arm(draft, origin, node.arms->fallthrough, fallthrough)].statement;
    const auto statement = Statement(IfStmt{*expression, drafts_[taken].statement, alternate}, origin);
    Append(draft, statement); tree_.operations.push_back({op, statement});
  }
  void EmitExceptionUnit(ControlId id, std::size_t draft) {
    const auto& unit = exceptions_->control.units[id.value];
    if (const auto* region = std::get_if<TryControlUnit>(&unit)) {
      EmitTry(region->region, draft);
      return;
    }
    if (std::holds_alternative<TryExitUnit>(unit)) return; // Normal end of this try arm, not JS break.
    const auto block = std::get<PhaseControlUnit>(unit).block;
    const auto& phase = graph_.phases[block.value];
    const auto& node = control_.nodes[id.value];
    if (phase.phase == analysis::ExceptionPhase::kCatch && catch_ranges_[block.value]) {
      const auto& range = forest_.catches[*catch_ranges_[block.value]];
      EmitAssignments(forest_.catch_assignments, range.first, range.count, draft);
    }
    if (phase.phase == analysis::ExceptionPhase::kCommit && phase.operation &&
        std::holds_alternative<analysis::ValueBranch>(graph_.ops[phase.operation->value].operation)) {
      EmitBranch(*phase.operation, node, graph_.blocks[block.value].first_edge, draft);
      return;
    }
    EmitBlock(node, draft);
  }
  void EmitTry(analysis::TryId region, std::size_t parent) {
    const auto& planned = exceptions_->control.regions[region.value];
    const auto& source = exceptions_->regions.regions[region.value];
    Require(source.binding && catch_symbols_[source.binding->block.value], Error::kBinding);
    const auto parameter = *catch_symbols_[source.binding->block.value];
    const TreeOriginId origin{static_cast<std::uint32_t>(tree_.origins.size())};
    Require(tree_.origins.size() < tree_.origins.capacity(), Error::kResourceLimit);
    tree_.origins.emplace_back(TryOrigin{region});
    const ScopeId body_scope{static_cast<std::uint32_t>(tree_.scopes.size())};
    tree_.scopes.push_back({ScopeKind::kBlock, drafts_[parent].scope});
    const ScopeId handler_scope{static_cast<std::uint32_t>(tree_.scopes.size())};
    tree_.scopes.push_back({ScopeKind::kCatch, drafts_[parent].scope});
    const auto body = Block(body_scope, origin), handler = Block(handler_scope, origin);
    Require(tree_.symbols[parameter.value].scope == kBodyScope, Error::kBinding);
    tree_.symbols[parameter.value].scope = handler_scope;
    Append(parent, Statement(TryStmt{drafts_[body].statement,
        CatchClause{parameter, drafts_[handler].statement}, {}}, origin));
    tasks_.push_back({planned.handler, handler});
    tasks_.push_back({planned.body, body});
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
          return local && local->allocation < site;
        });
    for (; cell != cells.end(); ++cell) {
      Step();
      const auto* local = std::get_if<AllocatedCell>(&cell->storage);
      if (!local || local->allocation != allocation) break;
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
      const auto header = exceptions_ ? ExceptionNodeBlock(*exceptions_, {loop.header.value}) :
          std::optional<analysis::WorkBlockId>{loop.header};
      Require(header.has_value(), Error::kOrigin);
      tree_.origins.emplace_back(LoopOrigin{*header});
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
    if (const auto* edge = std::get_if<analysis::EdgeId>(&transfer.source)) {
      const auto source = exceptions_ ? ExceptionArcOrigin(*exceptions_, {edge->value}) :
          std::optional<TreeOrigin>{EdgeOrigin{*edge}};
      Require(source.has_value(), Error::kOrigin);
      tree_.origins.push_back(*source);
    } else {
      const auto projected = control_.loops[std::get<LoopId>(transfer.source).value].header;
      const auto header = exceptions_ ? ExceptionNodeBlock(*exceptions_, {projected.value}) :
          std::optional<analysis::WorkBlockId>{projected};
      Require(header.has_value(), Error::kOrigin);
      tree_.origins.emplace_back(LoopOrigin{*header});
    }
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
    const auto checked = exceptions_
        ? VerifyExceptionTreeSemantics(graph_, *exceptions_, evaluation_, tree_, constants_,
                                       {bytes_ + storage->bytes, work_})
        : VerifyTreeSemantics(graph_, control_, evaluation_, tree_, constants_,
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
    CheckCaptures(*storage);
  }

  void CaptureNames() {
    if (!incoming_) return;
    const auto& cells = incoming_->cells;
    Require(syntax_.captures.empty() && syntax_.identity == cells.identity &&
        syntax_.method == cells.child && graph_.revision == cells.child_revision &&
        graph_.cfg_revision == cells.child_cfg, Error::kCapture);
    Require(syntax_.names.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    auto bound = BindCaptureAliases(cells, incoming_->parent, syntax_.owner,
        {static_cast<std::uint32_t>(syntax_.names.size())}, {bytes_, work_});
    const auto* error = std::get_if<CaptureTreeError>(&bound);
    if (error) {
      if (*error == CaptureTreeError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == CaptureTreeError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kCapture;
    }
    auto aliases = std::get<CaptureAliases>(std::move(bound));
    Admit(aliases.allocated_bytes, 1); Step(aliases.work);
    Grow(syntax_.names, syntax_.names.size() + aliases.names.size());
    for (auto& name : aliases.names) { Step(); syntax_.names.push_back(std::move(name)); }
    syntax_.captures = std::move(aliases.aliases);
    constants_.name_count = syntax_.names.size();
  }

  void CheckCaptures(const TreeStorage& storage) {
    if (!incoming_) return;
    const auto checked = VerifyTreeFlow(graph_, control_, evaluation_, tree_,
        {bytes_ + storage.bytes, work_});
    const auto* flow = std::get_if<TreeFlow>(&checked);
    if (!flow) {
      const auto error = std::get<TreeFlowError>(checked);
      if (error == TreeFlowError::kResourceLimit) throw Error::kResourceLimit;
      if (error == TreeFlowError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kCapture;
    }
    Require(flow->allocated_bytes >= storage.bytes, Error::kCapture);
    Admit(flow->allocated_bytes - storage.bytes, 1); Step(flow->work);
    const auto verified = VerifyCaptureTree(incoming_->cells, incoming_->parent, {tree_, *flow}, {bytes_, work_});
    if (const auto* error = std::get_if<CaptureTreeError>(&verified)) {
      if (*error == CaptureTreeError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == CaptureTreeError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kCapture;
    }
    Step(std::get<std::uint64_t>(verified));
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
  std::vector<NameId> reserved_names_;
  std::size_t next_binding_name_ = 0;
  std::vector<std::optional<std::size_t>> edge_ranges_;
  std::vector<std::optional<StmtId>> loops_;
  std::optional<std::size_t> entry_range_;
  const IncomingCaptureTree* incoming_;
  const ExceptionTreeInput* exceptions_;
  std::vector<std::optional<SymbolId>> catch_symbols_;
  std::vector<std::optional<std::size_t>> catch_ranges_, throw_ranges_;
  RecoveryMode recovery_;
};
}  // namespace

std::variant<ConstructedFunction, FunctionTreeError> BuildFunctionTree(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, FunctionSyntax syntax,
    ExpressionConstants constants, analysis::SemanticBudget budget,
    const IncomingCaptureTree* incoming, RecoveryMode recovery) {
  try {
    return TreeBuilder(graph, control, evaluation, std::move(syntax), constants, budget, incoming,
                       nullptr, recovery).Build();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<ExceptionFunctionDraft, FunctionTreeError> BuildExceptionFunctionDraft(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, FunctionSyntax syntax,
    ExpressionConstants constants, analysis::SemanticBudget budget) {
  try {
    auto built = TreeBuilder(graph, exceptions.control.skeleton, evaluation,
        std::move(syntax), constants, budget, nullptr, &exceptions).Build();
    return ExceptionFunctionDraft{std::move(built.tree), built.allocated_bytes, built.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
