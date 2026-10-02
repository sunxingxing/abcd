#include "default_candidates.h"

#include <array>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = DefaultCandidateError;
void Require(bool condition, Error error = Error::kInvalidGraph) {
  if (!condition) throw error;
}
struct BlockFacts {
  std::uint32_t predecessors = 0;
  std::uint32_t operations = 0;
  std::uint32_t environment_prefix = 0;
  std::optional<OpId> unused_literal;
  bool ambiguous_literal = false;
  std::uint32_t phis = 0;
  std::optional<OpId> last;
  std::uint32_t value_operations = 0;
  std::optional<OpId> value_op;
  std::optional<ValueId> phi;
  bool visited = false;
  std::optional<OpId> capture_load;
  std::optional<OpId> capture_guard;
  bool ambiguous_capture = false;
};
struct PhiFacts {
  std::array<std::optional<UseId>, 2> inputs;
  std::uint32_t count = 0;
};
class Finder {
 public:
  Finder(const ValueProgram& graph, const ParameterPlan& parameters,
         SemanticBudget budget) : graph_(graph), parameters_(parameters), initial_(budget), budget_(budget) {}
  DefaultCandidates Find(std::uint32_t length) {
    Require(parameters_.revision == graph_.revision && parameters_.cfg_revision == graph_.cfg_revision,
            Error::kRevision);
    Require(length <= parameters_.formals.size());
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    Require(parameters_.formals.size() <= kMaxId && graph_.ops.size() <= kMaxId &&
            graph_.uses.size() <= kMaxId, Error::kResourceLimit);
    DefaultCandidates result;
    result.revision = graph_.revision;
    result.cfg_revision = graph_.cfg_revision;
    Admit(1, sizeof(DefaultCandidates));
    if (length == parameters_.formals.size() || graph_.blocks.empty()) {
      result.allocated_bytes = initial_.bytes - budget_.bytes;
      return result;
    }
    Array(blocks_, graph_.blocks.size());
    Array(phis_, graph_.values.size());
    Index();
    Admit(parameters_.formals.size() - length, sizeof(DefaultCandidate));
    result.values.reserve(parameters_.formals.size() - length);
    Admit(result.values.capacity() - (parameters_.formals.size() - length), sizeof(DefaultCandidate));
    WorkBlockId header{0};
    std::uint32_t next_parameter = length;
    while (header.value < blocks_.size()) {
      Step();
      if (blocks_[header.value].visited) break;
      blocks_[header.value].visited = true;
      auto candidate = Match(header);
      if (!candidate || candidate->parameter < next_parameter ||
          (result.values.empty() && candidate->parameter != length)) break;
      result.values.push_back(*candidate);
      next_parameter = candidate->parameter + 1;
      header = candidate->join;
    }
    if (capture_ && capture_->shape.parameter == next_parameter) result.capture = capture_;
    // Persistent result only; scratch peak was admitted alongside it above.
    result.allocated_bytes = sizeof(DefaultCandidates) + result.values.capacity() * sizeof(DefaultCandidate);
    result.work = initial_.work - budget_.work;
    return result;
  }
 private:
  void Step(std::uint64_t count = 1) {
    Require(count <= budget_.work, Error::kWorkLimit); budget_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= budget_.bytes / width, Error::kResourceLimit); budget_.bytes -= count * width;
  }
  template <class T> void Array(std::vector<T>& values, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T)); Step(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  void Index() {
    for (const auto& edge : graph_.edges) {
      Step(); Require(edge.source.value < blocks_.size() && edge.target.value < blocks_.size());
      auto& count = blocks_[edge.target.value].predecessors;
      Require(count < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit); ++count;
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); const auto& op = graph_.ops[i];
      Require(op.block.value < blocks_.size());
      if (op.life == ValueLife::kRemoved && std::holds_alternative<ValueCopy>(op.operation) &&
          op.use_count == 1 && op.first_use.value < graph_.uses.size() &&
          !graph_.uses[op.first_use.value].input &&
          !std::holds_alternative<ValueId>(graph_.uses[op.first_use.value].operand)) {
        auto& facts = blocks_[op.block.value];
        if (facts.unused_literal) facts.ambiguous_literal = true;
        else facts.unused_literal = OpId{static_cast<std::uint32_t>(i)};
      }
      if (op.life != ValueLife::kLive || std::holds_alternative<ValueNop>(op.operation)) continue;
      auto& block = blocks_[op.block.value];
      Require(i <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
      if (op.block.value == 0 && block.environment_prefix == block.operations &&
          (std::holds_alternative<ValueEnvironmentPush>(op.operation) ||
           std::holds_alternative<ValueLexicalStore>(op.operation))) ++block.environment_prefix;
      ++block.operations; block.last = OpId{static_cast<std::uint32_t>(i)};
      if (std::holds_alternative<ValueLexicalLoad>(op.operation)) {
        if (block.capture_load) block.ambiguous_capture = true;
        else block.capture_load = block.last;
      }
      if (std::holds_alternative<ValueGuard>(op.operation)) {
        if (block.capture_guard) block.ambiguous_capture = true;
        else block.capture_guard = block.last;
      }
      if (!std::holds_alternative<ValueJump>(op.operation)) {
        ++block.value_operations; block.value_op = block.last;
      }
    }
    for (std::size_t i = 0; i < graph_.values.size(); ++i) {
      Step(); const auto& value = graph_.values[i];
      if (value.life != ValueLife::kLive) continue;
      if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) {
        Require(phi->block.value < blocks_.size());
        auto& block = blocks_[phi->block.value]; ++block.phis;
        block.phi = ValueId{static_cast<std::uint32_t>(i)};
      }
    }
    for (std::size_t i = 0; i < graph_.uses.size(); ++i) {
      Step(); const auto& use = graph_.uses[i];
      if (use.life != ValueLife::kLive) continue;
      if (const auto* phi = std::get_if<ValueId>(&use.owner)) {
        Require(phi->value < phis_.size());
        auto& inputs = phis_[phi->value];
        // Saturate at >2: only a two-predecessor join can match this candidate.
        if (inputs.count < inputs.inputs.size()) {
          Require(i <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
          inputs.inputs[inputs.count] = UseId{static_cast<std::uint32_t>(i)};
        }
        if (inputs.count <= inputs.inputs.size()) ++inputs.count;
      }
    }
  }
  const OperandUse& Use(const ValueOp& op, std::uint32_t ordinal) {
    Require(ordinal < op.use_count && op.first_use.value < graph_.uses.size() &&
        ordinal < graph_.uses.size() - op.first_use.value);
    const auto& use = graph_.uses[op.first_use.value + ordinal];
    Require(use.life == ValueLife::kLive && use.purpose == ReadPurpose::kOperand);
    return use;
  }
  const WorkEdge& Edge(const WorkBlock& block, std::uint32_t ordinal) {
    Require(ordinal < block.edge_count && block.first_edge.value < graph_.edges.size() &&
        ordinal < graph_.edges.size() - block.first_edge.value);
    return graph_.edges[block.first_edge.value + ordinal];
  }
  void CaptureBoundary(DefaultCandidate shape, const BlockFacts& facts) {
    if (!facts.capture_load || facts.ambiguous_capture) return;
    const auto& load = graph_.ops[facts.capture_load->value];
    const auto* value = std::get_if<ValueId>(&shape.fallback);
    if (!value || load.result != *value || load.use_count != 0 ||
        facts.value_operations != 1U + (facts.capture_guard ? 1U : 0U)) return;
    if (facts.capture_guard) {
      const auto& guard = graph_.ops[facts.capture_guard->value];
      const auto* guarded = guard.use_count == 1 ? std::get_if<ValueId>(&Use(guard,0).operand) : nullptr;
      if (guard.order <= load.order || !guarded || *guarded != *value || guard.result ||
          Use(guard,0).role != OperandRole::kGuardValue) return;
    }
    capture_ = CapturedDefaultCandidate{shape,
        std::get<ValueLexicalLoad>(load.operation).address,facts.capture_guard};
  }
  std::optional<DefaultCandidate> Match(WorkBlockId header) {
    const auto& facts = blocks_[header.value];
    const auto& block = graph_.blocks[header.value];
    if (header.value == 0 && facts.predecessors != 0) return {};
    constexpr std::uint32_t kCompareAndBranch = 2;
    const auto operations = facts.operations - facts.environment_prefix;
    if (operations < kCompareAndBranch || operations > kCompareAndBranch + 1 ||
        !facts.last || block.edge_count != 2) return {};
    const auto& branch = graph_.ops[facts.last->value];
    const auto* condition = std::get_if<ValueBranch>(&branch.operation);
    if (!condition || branch.use_count != 1) return {};
    const bool true_branch = condition->predicate == UnaryOperator::kTrueOrNonzeroNumber;
    if (!true_branch && condition->predicate != UnaryOperator::kFalseOrZero) return {};
    const auto* tested = std::get_if<ValueId>(&Use(branch, 0).operand);
    if (!tested) return {};
    Require(tested->value < graph_.values.size());
    const auto* producer = std::get_if<OpResult>(&graph_.values[tested->value].origin);
    if (!producer) return {};
    Require(producer->op.value < graph_.ops.size());
    const auto& comparison = graph_.ops[producer->op.value];
    const auto* binary = std::get_if<ValueBinary>(&comparison.operation);
    if (comparison.block != header || comparison.life != ValueLife::kLive || comparison.use_count != 2 ||
        !binary || (binary->op != BinaryOperator::kStrictEqual && binary->op != BinaryOperator::kStrictNotEqual)) return {};
    const auto& left = Use(comparison, 0).operand;
    const auto& right = Use(comparison, 1).operand;
    std::optional<OpId> undefined_op;
    const auto undefined = [&](const ValueOperand& operand) {
      if (const auto* special = std::get_if<SpecialValue>(&operand))
        return *special == SpecialValue::kUndefined;
      const auto* value = std::get_if<ValueId>(&operand);
      if (!value || value->value >= graph_.values.size()) return false;
      const auto* result = std::get_if<OpResult>(&graph_.values[value->value].origin);
      if (!result || result->op.value >= graph_.ops.size()) return false;
      const auto& op = graph_.ops[result->op.value];
      if (op.life != ValueLife::kLive || op.block != header || op.order >= comparison.order ||
          !std::holds_alternative<ValueCopy>(op.operation) || op.use_count != 1 || op.result != *value) return false;
      const auto* special = std::get_if<SpecialValue>(&Use(op, 0).operand);
      if (!special || *special != SpecialValue::kUndefined) return false;
      undefined_op = result->op;
      return true;
    };
    const auto* entry = std::get_if<ValueId>(undefined(left) ? &right : undefined(right) ? &left : nullptr);
    if (!entry || operations != kCompareAndBranch + (undefined_op ? 1U : 0U)) return {};
    Require(entry->value < graph_.values.size());
    const auto* definition = std::get_if<EntryDefinition>(&graph_.values[entry->value].origin);
    const auto* parameter = definition ? std::get_if<ParameterEntry>(definition) : nullptr;
    if (!parameter) return {};
    Require(parameter->index < parameters_.formals.size() &&
        parameters_.formals[parameter->index].entry == *entry);
    const bool missing_taken = (binary->op == BinaryOperator::kStrictEqual) == true_branch;
    const auto& first = Edge(block, 0);
    const auto& second = Edge(block, 1);
    if (first.kind == second.kind ||
        (first.kind != FlowEdgeKind::kTaken && first.kind != FlowEdgeKind::kFallthrough) ||
        (second.kind != FlowEdgeKind::kTaken && second.kind != FlowEdgeKind::kFallthrough)) return {};
    const auto missing_ordinal = (first.kind == FlowEdgeKind::kTaken) == missing_taken ? 0U : 1U;
    const EdgeId missing{block.first_edge.value + missing_ordinal};
    const EdgeId present{block.first_edge.value + (1U - missing_ordinal)};
    const auto arm = graph_.edges[missing.value].target;
    const auto join = graph_.edges[present.value].target;
    if (arm == join || arm == header || join == header || blocks_[arm.value].predecessors != 1 ||
        blocks_[join.value].predecessors != 2 || blocks_[arm.value].phis != 0 ||
        blocks_[join.value].phis > 1) return {};
    const auto& arm_block = graph_.blocks[arm.value];
    if (arm_block.edge_count != 1 || Edge(arm_block, 0).target != join) return {};
    // Register copies were removed, but literal-producing copies stay live.
    // Admit a literal producer, optional numeric negation and one jump.
    const auto& arm_facts = blocks_[arm.value];
    constexpr std::uint32_t kLiteralAndNegation = 2;
    if (arm_facts.value_operations > kLiteralAndNegation ||
        arm_facts.operations > arm_facts.value_operations + 1) return {};
    if (blocks_[join.value].phis == 0) {
      if (arm_facts.capture_load) {
        const auto& load = graph_.ops[arm_facts.capture_load->value];
        if (load.result) CaptureBoundary(DefaultCandidate{parameter->index,*entry,{},*load.result,*arm_facts.capture_load,
            producer->op,*facts.last,header,arm,join,present,missing,arm_block.first_edge,undefined_op,{}}, arm_facts);
        return {};
      }
      // Dead default assignments still carry their original literal operation.
      // Do not invent an initializer from length, or pick between several literals.
      if (arm_facts.value_operations != 0 || !arm_facts.unused_literal ||
          arm_facts.ambiguous_literal) return {};
      const auto initializer = *arm_facts.unused_literal;
      const auto& value = graph_.uses[graph_.ops[initializer.value].first_use.value].operand;
      return DefaultCandidate{parameter->index, *entry, {}, value, initializer,
          producer->op, *facts.last, header, arm, join, present, missing, arm_block.first_edge,
          undefined_op, {}};
    }
    const auto merged = *blocks_[join.value].phi;
    const auto& phi = phis_[merged.value];
    if (phi.count != 2) return {};
    const OperandUse* fallback = nullptr;
    bool incoming = false;
    for (const auto input : phi.inputs) {
      const auto& use = graph_.uses[input->value];
      const auto* edge = use.incoming ? std::get_if<EdgeId>(&*use.incoming) : nullptr;
      if (!edge || use.role != OperandRole::kPhiInput) return {};
      if (*edge == present) {
        const auto* value = std::get_if<ValueId>(&use.operand);
        if (incoming || !value || *value != *entry) return {};
        incoming = true;
      } else if (*edge == arm_block.first_edge) {
        if (fallback) return {};
        fallback = &use;
      } else return {};
    }
    if (!incoming || !fallback || !arm_facts.value_op) return {};
    auto literal = fallback->operand;
    if (arm_facts.capture_load) {
      CaptureBoundary(DefaultCandidate{parameter->index,*entry,merged,literal,*arm_facts.capture_load,
          producer->op,*facts.last,header,arm,join,present,missing,arm_block.first_edge,undefined_op,{}}, arm_facts);
      return {};
    }
    std::optional<OpId> number_op;
    {
      const auto& initializer = graph_.ops[arm_facts.value_op->value];
      const auto* unary = std::get_if<ValueUnary>(&initializer.operation);
      const auto* value = std::get_if<ValueId>(&literal);
      if (initializer.use_count != 1 || !value || initializer.result != *value) return {};
      const auto& input = Use(initializer, 0).operand;
      if (std::holds_alternative<ValueCopy>(initializer.operation)) {
        // Phi operands remain ValueIds even after copy propagation. Their
        // literal producer is retained; never expect a literal phi operand.
        if (std::holds_alternative<ValueId>(input)) return {};
        literal = input;
      } else {
        if (!unary || unary->op != UnaryOperator::kNegate) return {};
        const ValueOperand* numeric = &input;
        if (const auto* id = std::get_if<ValueId>(&input)) {
          if (id->value >= graph_.values.size()) return {};
          const auto* result = std::get_if<OpResult>(&graph_.values[id->value].origin);
          if (!result || result->op.value >= graph_.ops.size()) return {};
          const auto& constant = graph_.ops[result->op.value];
          if (constant.life != ValueLife::kLive || constant.block != arm ||
              constant.order >= initializer.order || constant.result != *id ||
              !std::holds_alternative<ValueCopy>(constant.operation) || constant.use_count != 1) return {};
          number_op = result->op;
          numeric = &Use(constant, 0).operand;
        }
        const auto* integer = std::get_if<IntegerLiteral>(numeric);
        if (!integer) return {};
        // JS Number negation preserves -0 and can exceed int32. Keep the
        // original operation for independent effect/exception refinement.
        literal = Float64Bits{std::bit_cast<std::uint64_t>(-static_cast<double>(integer->value))};
      }
    }
    if (arm_facts.value_operations != 1U + (number_op ? 1U : 0U)) return {};
    if (const auto* special = std::get_if<SpecialValue>(&literal);
        special && *special != SpecialValue::kUndefined && *special != SpecialValue::kNull &&
        *special != SpecialValue::kTrue && *special != SpecialValue::kFalse) return {};
    return DefaultCandidate{parameter->index, *entry, merged, literal, *arm_facts.value_op,
        producer->op, *facts.last, header, arm, join, present, missing, arm_block.first_edge,
        undefined_op, number_op};
  }
  const ValueProgram& graph_;
  const ParameterPlan& parameters_;
  SemanticBudget initial_;
  SemanticBudget budget_;
  std::vector<BlockFacts> blocks_;
  std::vector<PhiFacts> phis_;
  std::optional<CapturedDefaultCandidate> capture_;
};
}  // namespace
std::variant<DefaultCandidates, DefaultCandidateError> FindDefaultCandidates(
    const analysis::ValueProgram& graph, const ParameterPlan& parameters,
    std::uint32_t length, analysis::SemanticBudget budget) {
  try { return Finder(graph, parameters, budget).Find(length);
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
