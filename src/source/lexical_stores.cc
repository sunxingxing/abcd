#include "lexical_stores.h"

#include <bit>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = LexicalStoreError;
// RegisterSsa::Reads uses lower_bound and upper_bound over original reads.
constexpr std::uint64_t kReadBoundSearches = 2;
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
    std::size_t retained = 0;
    for (std::size_t i = 0; i < instructions_.size(); ++i) {
      Step(); pc_ = instructions_[i].pc;
      if (!std::holds_alternative<analysis::LexicalStore>(instructions_[i].operation)) continue;
      ++result_.stores;
      const bool captured = captured_[i];
      result_.captured += captured;
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
      const auto cell = *cells_->operations[operation->value];
      Require(cell.value < cells_->cells.size() && cell.value < flow.cells.size() && flow.cells[cell.value] &&
          std::holds_alternative<IncomingCell>(cells_->cells[cell.value].storage) == captured, Error::kStore);
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
        ++retained;
        continue;
      }
      const auto* assignment = std::get_if<AssignmentExpr>(&tree.expressions[expression.value].expression);
      Require(assignment != nullptr, Error::kStore);
      const auto* symbol = std::get_if<SymbolTarget>(&assignment->target);
      Require(symbol && symbol->symbol == *flow.cells[cell.value], Error::kStore);
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
  void Bind(NormalStoreInput input) {
    const auto& function = input.function;
    Require(target_.tree.identity == function.Identity() && target_.tree.method == function.Method(), Error::kOwner);
    instructions_ = function.Input().instructions;
    domains_ = function.Proof().values.Facts();
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
