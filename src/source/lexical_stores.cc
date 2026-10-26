#include "phased_parameter_defaults.h"
#include "lexical_stores.h"

#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = LexicalStoreError;
// RegisterSsa::Reads uses lower_bound and upper_bound over original reads.
constexpr std::uint64_t kReadBoundSearches = 2;
bool SameOperand(const analysis::ValueOperand& a,
                 const analysis::ValueOperand& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, analysis::SpecialValue>)
          return value == std::get<T>(b);
        else
          return value.value == std::get<T>(b).value;
      },
      a);
}
class StoreCheck {
 public:
  StoreCheck(LexicalStoreInput input, const analysis::ValueProgram& graph, const CellPlan* cells,
      CaptureTree target, std::span<const std::uint8_t> signature, analysis::SemanticBudget budget)
      : input_(input), graph_(graph), cells_(cells), target_(target), signature_(signature),
        initial_(budget), left_(budget) {}
  LexicalStores Run() {
    Admit(1, sizeof(StoreCheck));
    std::visit([&](const auto& input) { Bind(input); }, input_);
    Require(graph_.input.sites.size() == instructions_.size() &&
        (signature_.empty() || signature_.size() == instructions_.size()), Error::kCoverage);
    const auto& tree = target_.tree;
    const auto& flow = target_.flow;
    Require(tree.value_revision == graph_.revision && tree.cfg_revision == graph_.cfg_revision &&
        flow.value_revision == graph_.revision && flow.cfg_revision == graph_.cfg_revision, Error::kOwner);
    if (cells_) Require(cells_->revision == graph_.revision && cells_->cfg_revision == graph_.cfg_revision &&
        cells_->operations.size() == graph_.ops.size(), Error::kOwner);
    std::vector<std::uint8_t> phased_signature;
    if (std::holds_alternative<PhasedStoreInput>(input_) && tree.phased_defaults) {
      TreeCheckBudget q(left_); VerifyPhasedDefaultSignature(tree, q);
      Admit(q.Bytes(), 1); Step(q.Work());
      Array(phased_signature, instructions_.size());
      for (const auto& d : tree.phased_defaults->Evidence().defaults)
        for (const auto& commit : d.captures) {
          Step(); Require(commit.store.value < phased_signature.size() && !phased_signature[commit.store.value], Error::kStore);
          phased_signature[commit.store.value] = 1;
        }
      for (std::size_t i = 0; i < instructions_.size(); ++i) {
        Step(); Require(static_cast<bool>(phased_signature[i]) == (!signature_.empty() && static_cast<bool>(signature_[i])), Error::kCoverage);
      }
    }
    std::size_t retained = 0;
    for (std::size_t i = 0; i < instructions_.size(); ++i) {
      Step(); pc_ = instructions_[i].pc;
      if (!std::holds_alternative<analysis::LexicalStore>(instructions_[i].operation)) continue;
      ++result_.stores;
      const bool captured = captured_[i];
      result_.captured += captured;
      const bool moved = !phased_signature.empty() && phased_signature[i];
      if (moved) {
        Require(target_.tree.phased_defaults != nullptr && !captured, Error::kStore);
        const auto op = graph_.input.sites[i];
        Require(op && graph_.ops[op->value].life == analysis::ValueLife::kRemoved &&
            std::holds_alternative<analysis::ValueNop>(graph_.ops[op->value].operation), Error::kStore);
        const auto& original = target_.tree.phased_defaults->Original();
        const auto* plan = original.Cells(); Require(plan != nullptr, Error::kStore);
        Require(i < plan->operations.size() && plan->operations[i] &&
            std::holds_alternative<AllocatedCell>(plan->cells[plan->operations[i]->value].storage), Error::kStore);
        Require(cells_ != nullptr, Error::kStore);
        Step(1 + std::bit_width(cells_->cells.size()));
        Require(FindCell(*cells_, plan->cells[plan->operations[i]->value].storage).has_value(), Error::kStore);
        ++result_.signature; continue;
      }
      const auto reads = std::visit([&](const auto& input) { return Reads(input, i); }, input_);
      Require(!reads.empty(), Error::kDomain);
      for (const auto& read : reads) {
        Step(); Require(read.value.value < domains_.size() && domains_[read.value.value].IsLanguage(), Error::kDomain);
      }
      const bool signature = !signature_.empty() && signature_[i];
      const auto operation = graph_.input.sites[i];
      if (!operation) {
        Require(!captured && signature && std::holds_alternative<NormalStoreInput>(input_), Error::kStore);
        ++result_.signature; continue;
      }
      Require(!signature && operation->value < graph_.ops.size(), Error::kStore);
      const auto& op = graph_.ops[operation->value];
      Require(op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueLexicalStore>(op.operation) &&
          op.origin.value < graph_.origins.size(), Error::kStore);
      const auto& origin = graph_.origins[op.origin.value];
      Require(origin.site.value == i && origin.pc == *pc_ && origin.original == instructions_[i].origin &&
          origin.disposition == analysis::OriginDisposition::kRetained, Error::kStore);
      Require(cells_ && cells_->operations[operation->value] &&
          operation->value < flow.expressions.size() && flow.expressions[operation->value], Error::kStore);
      const auto cell = Correspondence(i, *operation, captured);
      Require(flow.cells[cell.value]->value < tree.symbols.size(), Error::kStore);
      const auto expression = *flow.expressions[operation->value];
      Require(expression.value < tree.expressions.size(), Error::kStore);
      // A deferred first store is the cell's own `let` declaration, which the
      // flow verifier bound as this cell's declaration point. Decide by that
      // statement, never by the initializer's shape (it may be an assignment).
      const auto statement = operation->value < flow.statements.size() ? flow.statements[operation->value] : std::nullopt;
      const auto* declaration = statement && statement->value < tree.statements.size()
          ? std::get_if<DeclarationStmt>(&tree.statements[statement->value].statement) : nullptr;
      if (declaration) {
        const auto* bindings = flow.original && flow.original->cells ? &flow.original->cells->bindings : nullptr;
        Require(!captured && bindings && cell.value < bindings->size() &&
            (*bindings)[cell.value].declaration == statement && declaration->initializer == expression &&
            declaration->symbol == *flow.cells[cell.value], Error::kStore);
        StoredInput(*operation, declaration->symbol, expression, *statement);
        ++retained;
        continue;
      }
      const auto* assignment = std::get_if<AssignmentExpr>(&tree.expressions[expression.value].expression);
      Require(assignment != nullptr, Error::kStore);
      const auto* symbol = std::get_if<SymbolTarget>(&assignment->target);
      Require(symbol && symbol->symbol == *flow.cells[cell.value], Error::kStore);
      StoredInput(*operation, symbol->symbol, assignment->value, expression);
      ++retained;
    }
    pc_.reset();
    std::size_t stores = 0;
    for (const auto& op : graph_.ops) {
      Step(); stores += op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueLexicalStore>(op.operation);
    }
    Require(stores == retained, Error::kStore);
    result_.allocated_bytes = initial_.bytes - left_.bytes;
    result_.work = initial_.work - left_.work;
    return result_;
  }
 private:
  // CellId is local to a plan. Descendant requirements may insert cells in
  // the final evaluation plan, so join at the original store via environment
  // ancestry/allocation identity, never by the source plan's numeric CellId.
  void IndexCorrespondence() {
    const auto& original = target_.flow.original;
    Require(original && original->cells && original->environments &&
                original->identity == target_.tree.identity &&
                original->method == target_.tree.method &&
                original->value_revision == graph_.revision &&
                original->cfg_revision == graph_.cfg_revision,
            Error::kOwner);
    const auto& states = *original->environments;
    Require(states.revision == graph_.revision &&
                states.cfg_revision == graph_.cfg_revision &&
                original->operations.size() == graph_.ops.size() &&
                original->phases.size() == graph_.phases.size() &&
                original->exceptional_exits.size() ==
                    graph_.exceptional_exits.size(),
            Error::kStore);
    Array(input_allocations_, input_environments_->environments.Size());
    Array(output_allocations_, states.flow.environments.Size());
    for (const auto& allocation : input_environments_->allocations) {
      Step();
      auto site = allocation.instruction;
      if (std::holds_alternative<PhasedStoreInput>(input_)) {
        const auto input = std::get<PhasedStoreInput>(input_);
        const auto& nodes =
            input.function.TreeInput(input.body).input.flow.nodes;
        Require(site < nodes.size(), Error::kStore);
        site = nodes[site].instruction.value;
      }
      Require(allocation.environment.value < input_allocations_.size() &&
                  site < graph_.input.sites.size() &&
                  graph_.input.sites[site] &&
                  !input_allocations_[allocation.environment.value],
              Error::kStore);
      input_allocations_[allocation.environment.value] =
          *graph_.input.sites[site];
    }
    for (const auto& allocation : states.flow.allocations) {
      Step();
      Require(allocation.environment.value < output_allocations_.size() &&
                  allocation.instruction < graph_.ops.size() &&
                  !output_allocations_[allocation.environment.value],
              Error::kStore);
      output_allocations_[allocation.environment.value] =
          analysis::OpId{allocation.instruction};
    }
    Array(declarations_, target_.tree.statements.size());
    for (std::size_t c = 0; c < original->cells->bindings.size(); ++c) {
      Step();
      const auto& binding = original->cells->bindings[c];
      if (!binding.declaration) continue;
      Require(binding.declaration->value < declarations_.size() &&
                  !declarations_[binding.declaration->value],
              Error::kStore);
      declarations_[binding.declaration->value] =
          CellId{static_cast<std::uint32_t>(c)};
    }
    Array(protocol_inputs_, graph_.uses.size());
    const auto protocol_input = [&](analysis::UseId use, ExprId expression) {
      Step(); Require(use.value < protocol_inputs_.size(), Error::kStore);
      auto& slot = protocol_inputs_[use.value];
      Require(!slot || *slot == expression, Error::kStore); slot = expression;
    };
    if (original->iterations) for (const auto& p : original->iterations->inputs) protocol_input(p.use, p.expression);
    for (const auto& p : original->enumeration_inputs) protocol_input(p.use, p.expression);
    Array(inputs_, graph_.uses.size());
    for (const auto& input : original->inputs) {
      Step();
      if (!input.occurrence) continue;
      Require(input.occurrence->value < inputs_.size(), Error::kStore);
      const auto& use = graph_.uses[input.occurrence->value];
      const auto* owner = std::get_if<analysis::OpId>(&use.owner);
      if (!owner || owner->value >= graph_.ops.size() ||
          !std::holds_alternative<analysis::ValueLexicalStore>(
              graph_.ops[owner->value].operation))
        continue;
      // One use may have several checked representations. Stores have one
      // physical input; reject ambiguity rather than choose one by spelling.
      auto& entry = inputs_[input.occurrence->value];
      if (!entry)
        entry = &input;
      else
        Require(entry->expression == input.expression &&
                    SameOperand(entry->operand, input.operand),
                Error::kStore);
    }
    for (std::size_t i = 0; i < graph_.phases.size(); ++i) {
      Step();
      const auto& a = graph_.phases[i];
      const auto& b = original->phases[i];
      Require(a.input == b.input && a.instruction == b.instruction &&
                  a.phase == b.phase && a.operation == b.operation &&
                  a.reachability == b.reachability,
              Error::kStore);
    }
    for (std::size_t i = 0; i < graph_.exceptional_exits.size(); ++i) {
      Step();
      const auto& a = graph_.exceptional_exits[i];
      const auto& b = original->exceptional_exits[i];
      Require(a.op == b.op && a.origin == b.origin &&
                  a.before_throw == b.before_throw && a.handler == b.handler,
              Error::kStore);
    }
    indexed_ = true;
  }
  CellStorage Storage(
      const analysis::LexicalBinding& binding,
      std::span<const std::optional<analysis::OpId>> allocations) {
    if (const auto* depth =
            std::get_if<analysis::CapturedDepth>(&binding.environment))
      return IncomingCell{*depth, binding.slot};
    const auto env = std::get<analysis::EnvironmentId>(binding.environment);
    Require(env.value < allocations.size() && allocations[env.value],
            Error::kStore);
    return AllocatedCell{*allocations[env.value], binding.slot};
  }
  bool SameStorage(const CellStorage& a, const CellStorage& b) {
    if (const auto* x = std::get_if<IncomingCell>(&a)) {
      const auto* y = std::get_if<IncomingCell>(&b);
      return y && x->depth == y->depth && x->slot.value == y->slot.value;
    }
    const auto& x = std::get<AllocatedCell>(a);
    const auto* y = std::get_if<AllocatedCell>(&b);
    return y && x.allocation == y->allocation && x.slot.value == y->slot.value;
  }
  CellId Correspondence(std::size_t site, analysis::OpId operation,
                        bool captured) {
    if (!indexed_) IndexCorrespondence();
    const auto& original = *target_.flow.original;
    const auto& states = *original.environments;
    const auto& raw =
        std::get<analysis::LexicalStore>(instructions_[site].operation);
    const auto& working = std::get<analysis::ValueLexicalStore>(
        graph_.ops[operation.value].operation);
    Require(raw.address.depth.value == working.address.depth.value &&
                raw.address.slot.value == working.address.slot.value,
            Error::kStore);
    auto input_site = site;
    if (std::holds_alternative<PhasedStoreInput>(input_)) {
      Require(before_[site].has_value(), Error::kStore);
      input_site = before_[site]->value;
    }
    Require(input_site < input_environments_->before.size() &&
                input_environments_->before[input_site] &&
                operation.value < states.flow.before.size() &&
                states.flow.before[operation.value],
            Error::kStore);
    Step(2 * std::numeric_limits<std::uint32_t>::digits);
    const auto a = input_environments_->environments.Resolve(
        *input_environments_->before[input_site], raw.address);
    const auto b = states.flow.environments.Resolve(
        *states.flow.before[operation.value], working.address);
    const auto* input = std::get_if<analysis::LexicalBinding>(&a);
    const auto* output = std::get_if<analysis::LexicalBinding>(&b);
    Require(input && output, Error::kStore);
    const auto storage = Storage(*input, input_allocations_);
    const auto source = *cells_->operations[operation.value];
    Require(source.value < cells_->cells.size() &&
                SameStorage(storage, cells_->cells[source.value].storage) &&
                std::holds_alternative<IncomingCell>(storage) == captured &&
                SameStorage(storage, Storage(*output, output_allocations_)),
            Error::kStore);
    // The final reference supplies a final-plan CellId only after its storage
    // and original operation have been independently joined above.
    const auto& cells = *original.cells;
    Require(operation.value < cells.references.size(), Error::kStore);
    const auto& reference = cells.references[operation.value];
    const auto statement = operation.value < target_.flow.statements.size()
                               ? target_.flow.statements[operation.value]
                               : std::nullopt;
    if (!reference) {
      // Deferred declarations own their initializer, not an assignment ExprId.
      // Their final binding is indexed once by the verified declaration below.
      Require(statement && statement->value < declarations_.size(),
              Error::kStore);
      const auto found = declarations_[statement->value];
      Require(found && found->value < cells.bindings.size() &&
                  found->value < target_.flow.cells.size(),
              Error::kStore);
      const auto& binding = cells.bindings[found->value];
      Require(binding.storage.environment == output->environment &&
                  binding.storage.slot.value == output->slot.value &&
                  target_.flow.cells[found->value] == binding.binding,
              Error::kStore);
      return *found;
    }
    Require(reference->kind == CellAccessKind::kStore &&
                reference->expression ==
                    *target_.flow.expressions[operation.value] &&
                reference->cell.value < cells.bindings.size(),
            Error::kStore);
    const auto& binding = cells.bindings[reference->cell.value];
    Require(binding.storage.environment == output->environment &&
                binding.storage.slot.value == output->slot.value &&
                reference->cell.value < target_.flow.cells.size() &&
                target_.flow.cells[reference->cell.value] == binding.binding,
            Error::kStore);
    return reference->cell;
  }
  template <class Site>
  void StoredInput(analysis::OpId operation, SymbolId target, ExprId rhs,
                   Site site) {
    const auto& op = graph_.ops[operation.value];
    const auto& original = *target_.flow.original;
    const auto& retained = original.operations[operation.value];
    Require(op.use_count == 1 && op.first_use.value < graph_.uses.size() &&
                retained.block == op.block &&
                retained.first_use == op.first_use &&
                retained.use_count == op.use_count &&
                retained.origin == op.origin && retained.life == op.life &&
                retained.result == op.result && retained.normal == op.normal &&
                retained.before_throw == op.before_throw &&
                retained.exceptional == op.exceptional &&
                retained.write == OriginalWriteSite{site},
            Error::kStore);
    const auto& use = graph_.uses[op.first_use.value];
    const auto* input = inputs_[op.first_use.value];
    // A stored loop binding (for-of/for-in value copied into a per-iteration
    // cell) is recorded by its protocol input, already verified as a read of
    // the loop variable; it is never an ordinary input occurrence.
    const bool protocol = !input && op.first_use.value < protocol_inputs_.size() &&
        protocol_inputs_[op.first_use.value] && *protocol_inputs_[op.first_use.value] == rhs;
    Require(
        use.owner ==
                std::variant<analysis::OpId, analysis::ValueId>{operation} &&
            use.role == analysis::OperandRole::kStoredValue &&
            use.purpose == analysis::ReadPurpose::kOperand &&
            ((input && input->expression == rhs && SameOperand(input->operand, use.operand)) || protocol),
        Error::kStore);
    const auto& definitions = [&]() -> const auto& {
      if constexpr (std::is_same_v<Site, ExprId>)
        return original.definitions;
      else
        return original.declarations;
    }();
    Require(site.value < definitions.size() && definitions[site.value],
            Error::kStore);
    const auto& definition = *definitions[site.value];
    const auto* saved =
        std::get_if<OriginalSavedDefinition>(&definition.origin);
    Require(saved && std::get_if<analysis::OpId>(&saved->placement) &&
                std::get<analysis::OpId>(saved->placement) == operation &&
                SameOperand(saved->operand, use.operand) &&
                definition.binding == target && definition.input == rhs,
            Error::kStore);
  }
  void Bind(NormalStoreInput input) {
    const auto& function = input.function;
    Require(target_.tree.identity == function.Identity() && target_.tree.method == function.Method(), Error::kOwner);
    instructions_ = function.Input().instructions;
    domains_ = function.Proof().values.Facts();
    input_environments_ = &function.Proof().environments;
    Array(captured_, instructions_.size());
    std::optional<analysis::InstructionId> previous;
    for (const auto& access : function.CaptureAccesses()) {
      Step(); Require(access.instruction.value < instructions_.size() &&
          (!previous || *previous < access.instruction), Error::kCoverage);
      previous = access.instruction;
      const auto& operation = instructions_[access.instruction.value].operation;
      Require((access.kind == analysis::LocalAccessKind::kStore && std::holds_alternative<analysis::LexicalStore>(operation)) ||
          (access.kind == analysis::LocalAccessKind::kLoad && std::holds_alternative<analysis::LexicalLoad>(operation)), Error::kCoverage);
      captured_[access.instruction.value] = 1;
    }
  }
  void Bind(PhasedStoreInput owner) {
    Require(target_.tree.identity == owner.function.Identity() && target_.tree.method == owner.function.Target(), Error::kOwner);
    const auto input = owner.function.TreeInput(owner.body).input;
    instructions_ = input.flow.instructions; domains_ = input.values.Facts();
    input_environments_ = &input.environments;
    Require(domains_.size() == input.ssa.Values().size(), Error::kDomain);
    Array(captured_, instructions_.size()); Array(before_, instructions_.size());
    for (std::size_t i = 0; i < input.flow.nodes.size(); ++i) {
      Step(); const auto& node = input.flow.nodes[i];
      Require(node.instruction.value < instructions_.size(), Error::kCoverage);
      if (node.phase != analysis::ExceptionPhase::kBefore) continue;
      Require(!before_[node.instruction.value], Error::kCoverage);
      before_[node.instruction.value] = analysis::ExceptionNodeId{static_cast<std::uint32_t>(i)};
    }
    for (const auto& binding : input.environments.bindings) {
      Step(); Require(binding.instruction < input.flow.nodes.size(), Error::kCoverage);
      const auto& node = input.flow.nodes[binding.instruction];
      Require(node.phase == analysis::ExceptionPhase::kBefore, Error::kCoverage);
      captured_[node.instruction.value] = std::holds_alternative<analysis::CapturedDepth>(binding.binding.environment);
    }
  }
  std::span<const analysis::SsaRead> Reads(NormalStoreInput input, std::size_t site) {
    Step(kReadBoundSearches * (std::bit_width(input.function.Input().ssa.AllReads().size()) + 1));
    return input.function.Input().ssa.Reads({static_cast<std::uint32_t>(site)});
  }
  std::span<const analysis::SsaRead> Reads(PhasedStoreInput owner, std::size_t site) {
    const auto input = owner.function.TreeInput(owner.body).input;
    Require(before_[site].has_value(), Error::kCoverage);
    // Never use Commit (normal result) or Catch (pending exception) as this
    // store's operand domain. The original Before SSA is the sole authority.
    Step(kReadBoundSearches * (std::bit_width(input.ssa.AllReads().size()) + 1));
    return input.ssa.Reads(*before_[site]);
  }
  void Step(std::uint64_t work = 1) { Require(work <= left_.work, Error::kWorkLimit); left_.work -= work; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count); Admit(values.capacity() - count, sizeof(T));
  }
  void Require(bool ok, Error error) const { if (!ok) throw LexicalStoreFailure{error, pc_}; }
  LexicalStoreInput input_;
  const analysis::ValueProgram& graph_;
  const CellPlan* cells_;
  CaptureTree target_;
  std::span<const std::uint8_t> signature_;
  analysis::SemanticBudget initial_, left_;
  std::span<const analysis::SemanticInstruction> instructions_;
  std::span<const analysis::ValueKinds> domains_;
  std::vector<std::uint8_t> captured_;
  std::vector<std::optional<analysis::ExceptionNodeId>> before_;
  const analysis::EnvironmentFlow* input_environments_ = nullptr;
  std::vector<std::optional<analysis::OpId>> input_allocations_,
      output_allocations_;
  std::vector<const OriginalInput*> inputs_;
  std::vector<std::optional<ExprId>> protocol_inputs_;
  std::vector<std::optional<CellId>> declarations_;
  bool indexed_ = false;
  std::optional<abc::CodeOffset> pc_;
  LexicalStores result_;
};
}  // namespace
std::variant<LexicalStores, LexicalStoreFailure> VerifyLexicalStores(
    LexicalStoreInput input, const analysis::ValueProgram& graph, const CellPlan* cells,
    CaptureTree target, std::span<const std::uint8_t> signature, analysis::SemanticBudget budget) {
  try { return StoreCheck(input, graph, cells, target, signature, budget).Run();
  } catch (const LexicalStoreFailure& error) { return error;
  } catch (const std::bad_alloc&) { return LexicalStoreFailure{Error::kResourceLimit, {}};
  } catch (const std::length_error&) { return LexicalStoreFailure{Error::kResourceLimit, {}}; }
}
}  // namespace abcd::source
