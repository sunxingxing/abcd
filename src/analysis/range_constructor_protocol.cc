#include <algorithm>
#include <bit>
#include <memory_resource>
#include <set>

#include "abcd/analysis/derived_constructor.h"
#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
namespace {
using Error = DerivedConstructorError;
// Each VM instruction has one observation companion; the two auxiliary
// registers describe the receiver binding and the pending construction.
constexpr std::uint32_t kObservationRegisters = 2;
constexpr std::uint32_t kSitesPerInstruction = 2;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
class Quota : public std::pmr::memory_resource {
 public:
  explicit Quota(DerivedConstructorBudget budget)
      : initial(budget), left(budget) {}
  void Step(std::uint64_t n = 1) {
    Require(n <= left.work, Error::kResourceLimit);
    left.work -= n;
  }
  void Admit(std::size_t n, std::size_t width = 1) {
    Require(n <= left.bytes / width, Error::kResourceLimit);
    left.bytes -= n * width;
  }
  template <class T>
  void Array(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T));
    Step(n);
    v.resize(n);
    Admit(v.capacity() - n, sizeof(T));
  }
  template <class T>
  void Reserve(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T));
    v.reserve(n);
    Admit(v.capacity() - n, sizeof(T));
  }
  DerivedConstructorBudget initial, left;

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    Admit(bytes);
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }
  void do_deallocate(void* p, std::size_t bytes,
                     std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
  }
  bool do_is_equal(const memory_resource& other) const noexcept override {
    return this == &other;
  }
};
bool Copy(const SemanticInstruction& instruction) {
  const auto* assign = std::get_if<ScalarAssignment>(&instruction.operation);
  return assign && std::holds_alternative<ScalarValue>(assign->expression) &&
         instruction.effects.normal == LanguageEffect::kNone &&
         instruction.effects.exceptional == ExceptionalCompletion::kImpossible;
}
enum class Pending : std::uint8_t { kNone = 1, kConstructed = 2 };
struct PathState {
  std::uint8_t receiver = 0, pending = 0;
};
struct EdgeState {
  std::uint8_t receiver = 0, pending = 0;
};
// An auxiliary receiver register is an analysis observation, never a VM write.
// At a successful mode-1 check it records the pending construction result.
// Comparing its SSA equation with the actual input proves receiver identity
// across copies, branch joins and loop-carried phis, rather than matching the
// compiler's register allocation or instruction adjacency.
class Protocol {
 public:
  Protocol(const abc::MethodBody& body, const abc::FrameLayout& frame,
           abc::ProfileId profile, Quota& quota)
      : body_(body),
        frame_(frame),
        profile_(profile),
        quota_(quota),
        pairs_(&quota),
        defined_(&quota),
        owners_(&quota) {}
  RangeDerivedConstructor Run() {
    Require(frame_.this_value && frame_.function && frame_.new_target &&
                frame_.this_value->value < frame_.registers &&
                frame_.registers < UINT32_MAX - kObservationRegisters,
            Error::kFrame);
    Require(body_.tries.empty() && body_.handlers.empty() &&
                !body_.instructions.empty(),
            Error::kControlFlow);
    Require(body_.instructions.size() <= UINT32_MAX / kSitesPerInstruction,
            Error::kResourceLimit);
    auto built = BuildNormalFlow(body_, {quota_.left.bytes});
    if (const auto* error = std::get_if<FlowFailure>(&built))
      throw error->kind == FlowError::kResourceLimit ? Error::kResourceLimit
                                                     : Error::kControlFlow;
    graph_ = std::get<NormalFlow>(std::move(built));
    quota_.Admit(graph_.blocks.capacity(), sizeof(FlowBlock));
    const auto n = body_.instructions.size();
    quota_.Array(instructions_, n);
    quota_.Array(sites_, n * kSitesPerInstruction);
    quota_.Array(blocks_, n);
    quota_.Array(checks_, n);
    quota_.Array(incoming_, graph_.blocks.size());
    quota_.Array(at_, n);
    quota_.Array(edges_, graph_.blocks.size() * kMaxNormalSuccessors);
    const abc::RegisterId receiver{frame_.registers},
        pending{frame_.registers + 1};
    std::size_t calls = 0, checks = 0, returns = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
      quota_.Step();
      auto lifted = LiftSemanticInstruction(body_.instructions[i], profile_,
                                            frame_.registers, &frame_);
      if (const auto* error = std::get_if<SemanticFailure>(&lifted))
        throw error->kind == SemanticError::kResourceLimit
            ? Error::kResourceLimit
            : Error::kInstruction;
      instructions_[i] = std::get<SemanticInstruction>(std::move(lifted));
      auto effect = instructions_[i].effects.normal_registers;
      const auto& op = instructions_[i].operation;
      Require(!std::holds_alternative<SuperSpread>(op) &&
                  !std::holds_alternative<SuperForward>(op),
              Error::kInstruction);
      if (const auto* call = std::get_if<SuperRange>(&op)) {
        Require(call->callee == SuperCalleeSource::kInvocationFunction,
                Error::kValue);
        ++calls;
        sites_[i * kSitesPerInstruction + 1].effect = {};
        sites_[i * kSitesPerInstruction + 1].effect.reads[0] = Accumulator{};
        sites_[i * kSitesPerInstruction + 1].effect.read_count = 1;
        sites_[i * kSitesPerInstruction + 1].effect.write = pending;
      }
      if (const auto* check = std::get_if<SuperCheck>(&op)) {
        ++checks;
        Require(effect.read_count == 1 && !effect.write, Error::kInstruction);
        effect.reads[effect.read_count++] = receiver;
        if (check->kind == SuperCheckKind::kRequireUninitialized) {
          effect.reads[effect.read_count++] = pending;
          effect.write = receiver;
        }
      }
      if (std::holds_alternative<ReturnOperation>(op)) ++returns;
      // PCs are observation identities in this auxiliary program, not encoded
      // bytecode PCs. The immutable body retains all original provenance.
      sites_[i * kSitesPerInstruction].pc = {i * kSitesPerInstruction};
      sites_[i * kSitesPerInstruction].effect = effect;
      sites_[i * kSitesPerInstruction + 1].pc = {i * kSitesPerInstruction + 1};
    }
    Require(calls != 0, Error::kInstruction);
    quota_.Reserve(proof_.sites, calls);
    quota_.Reserve(proof_.coverage, checks);
    quota_.Reserve(proof_.returns, returns);
    NormalFlow probe_graph;
    quota_.Array(probe_graph.blocks, graph_.blocks.size());
    std::copy(graph_.blocks.begin(), graph_.blocks.end(),
              probe_graph.blocks.begin());
    for (std::uint32_t b = 0; b < graph_.blocks.size(); ++b) {
      const auto& block = graph_.blocks[b];
      quota_.Step();
      for (std::size_t i = block.first_instruction;
           i < std::size_t(block.first_instruction) + block.instruction_count;
           ++i)
        blocks_[i] = {b};
      probe_graph.blocks[b].first_instruction *= kSitesPerInstruction;
      probe_graph.blocks[b].instruction_count *= kSitesPerInstruction;
    }
    IdfQuota work{quota_.left.work};
    auto ssa = RegisterSsa::BuildSites(
        sites_, probe_graph,
        std::uint64_t(frame_.registers) + kObservationRegisters, work,
        {quota_.left.bytes});
    if (const auto* error = std::get_if<RegisterUseFailure>(&ssa))
      throw error->kind == RegisterUseError::kResourceLimit ||
              error->kind == RegisterUseError::kWorkLimit
          ? Error::kResourceLimit
          : Error::kInstruction;
    if (const auto* error = std::get_if<IdfError>(&ssa))
      throw *error == IdfError::kResourceLimit || *error == IdfError::kWorkLimit
          ? Error::kResourceLimit
          : Error::kControlFlow;
    if (const auto* error = std::get_if<DominatorError>(&ssa))
      throw *error == DominatorError::kResourceLimit ||
              *error == DominatorError::kWorkLimit
          ? Error::kResourceLimit
          : Error::kControlFlow;
    if (std::holds_alternative<SsaError>(ssa)) throw Error::kResourceLimit;
    ssa_.emplace(std::get<RegisterSsa>(std::move(ssa)));
    quota_.Admit(ssa_->AllocatedBytes());
    quota_.Step(work.Consumed());
    quota_.Array(roots_, ssa_->Values().size());
    quota_.Array(copy_, roots_.size());
    for (std::uint32_t v = 0; v < roots_.size(); ++v) {
      quota_.Step();
      roots_[v] = {v};
      const auto& value = ssa_->Values()[v];
      if (const auto* entry = std::get_if<MethodEntry>(&value.origin)) {
        static_cast<void>(entry);
        const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
        if (reg && (*reg == receiver || *reg == *frame_.this_value)) {
          if (entry_this_)
            copy_[v] = *entry_this_;
          else
            entry_this_ = SsaValueId{v};
        }
      } else if (const auto* site =
                     std::get_if<InstructionValue>(&value.origin)) {
        const auto i = site->instruction.value / kSitesPerInstruction;
        const auto reads = Reads(site->instruction);
        if (site->instruction.value % kSitesPerInstruction ||
            Copy(instructions_[i])) {
          if (reads.size() == 1) copy_[v] = reads[0].value;
        } else if (const auto* check =
                       std::get_if<SuperCheck>(&instructions_[i].operation);
                   check &&
                   check->kind == SuperCheckKind::kRequireUninitialized) {
          Require(reads.size() == 3, Error::kInstruction);
          copy_[v] = reads[2].value;
        }
      }
    }
    // Copy edges are acyclic in valid SSA except through phis. Iterative path
    // compression visits each copy once and never recursively walks the CFG.
    std::vector<std::uint8_t> done;
    quota_.Array(done, roots_.size());
    std::vector<SsaValueId> path;
    quota_.Reserve(path, roots_.size());
    for (std::uint32_t v = 0; v < roots_.size(); ++v) {
      if (done[v]) continue;
      auto at = SsaValueId{v};
      path.clear();
      while (copy_[at.value] && !done[at.value]) {
        quota_.Step();
        done[at.value] = 1;
        path.push_back(at);
        at = *copy_[at.value];
      }
      Require(!copy_[at.value] || done[at.value] == 2, Error::kValue);
      const auto root = roots_[at.value];
      done[at.value] = 2;
      while (!path.empty()) {
        const auto x = path.back();
        path.pop_back();
        roots_[x.value] = root;
        done[x.value] = 2;
      }
    }
    pair_limit_ = ssa_->Values().size() + ssa_->AllInputs().size() +
                  ssa_->AllReads().size();
    quota_.Reserve(pair_queue_, pair_limit_);
    quota_.Reserve(proof_.bindings, pair_limit_);
    // Bound the size of relational equation exploration by its indexed input.
    // Dense congruence products exhaust this typed work resource rather than
    // falling back to a quadratic producer/path scan.
    // Three indexed relations (receiver equality, construction ownership and
    // return definedness) each admit node/input/queue visits. Denser products
    // need a different representation, not an unbounded per-query graph walk.
    constexpr std::uint64_t kEquationVisitsPerInput = 8;
    Require(pair_limit_ <= UINT64_MAX / kEquationVisitsPerInput,
            Error::kResourceLimit);
    equations_ = pair_limit_ * kEquationVisitsPerInput;
    // Verify every guard operand against the same shadow binding. A failed
    // comparison never licenses an elision, including in unreachable blocks.
    for (std::uint32_t i = 0; i < n; ++i)
      if (const auto* check =
              std::get_if<SuperCheck>(&instructions_[i].operation)) {
        quota_.Step();
        const auto reads = Reads({i * kSitesPerInstruction});
        Require(reads.size() >= 2, Error::kGuard);
        if (check->kind != SuperCheckKind::kNoop)
          Equal(reads[0].value, reads[1].value);
        if (check->kind == SuperCheckKind::kRequireUninitialized) {
          const auto first = proof_.bindings.size();
          ConstructionOwners({i}, reads[2].value);
          Require(first != proof_.bindings.size(), Error::kGuard);
          if (proof_.bindings.size() == first + 1)
            checks_[i] = proof_.bindings[first].construction;
        }
      }
    SolvePaths();
    CheckPendingCycles();
    BuildRefinements();
    for (std::uint32_t i = 0; i < n; ++i) {
      quota_.Step();
      const auto& op = instructions_[i].operation;
      if (std::holds_alternative<SuperRange>(op))
        proof_.sites.push_back({{i}, body_.instructions[i].offset});
      if (const auto* check = std::get_if<SuperCheck>(&op))
        proof_.coverage.push_back(
            {{i},
             check->kind == SuperCheckKind::kRequireUninitialized
                 ? DerivedCheckAction::kBind
             : check->kind == SuperCheckKind::kRequireInitialized
                 ? DerivedCheckAction::kRead
                 : DerivedCheckAction::kNoop,
             checks_[i]});
      if (std::holds_alternative<ReturnOperation>(op)) {
        // An uninitialized path must have returned a defined value. Prove
        // its SSA contributions under the actual incoming edge domains.
        if (at_[i].receiver & kUninitialized)
          Require(
              Defined(Reads({i * kSitesPerInstruction})[0].value, blocks_[i]),
              Error::kReturn);
        proof_.returns.push_back(
            {{i}, static_cast<ReceiverState>(at_[i].receiver)});
      }
    }
    proof_.allocated_bytes =
        proof_.sites.capacity() * sizeof(DerivedSuperSite) +
        proof_.coverage.capacity() * sizeof(DerivedCoverage) +
        proof_.returns.capacity() * sizeof(DerivedReturnSite) +
        proof_.bindings.capacity() * sizeof(DerivedBindingSite);
    proof_.work = quota_.initial.work - quota_.left.work;
    return std::move(proof_);
  }

 private:
  static constexpr std::uint8_t kUninitialized =
      static_cast<std::uint8_t>(ReceiverState::kUninitialized);
  static constexpr std::uint8_t kInitialized =
      static_cast<std::uint8_t>(ReceiverState::kInitialized);
  SsaValueId Root(SsaValueId v) const { return roots_[v.value]; }
  std::span<const SsaRead> Reads(InstructionId site) {
    quota_.Step(1 + std::bit_width(ssa_->AllReads().size()));
    return ssa_->Reads(site);
  }
  std::span<const PhiInput> Inputs(SsaValueId value) {
    quota_.Step(1 + std::bit_width(ssa_->AllInputs().size()));
    return ssa_->Inputs(value);
  }
  void EquationStep() {
    Require(equations_ != 0, Error::kResourceLimit);
    --equations_;
    quota_.Step();
  }
  void ConstructionOwners(InstructionId guard, SsaValueId value) {
    std::pmr::vector<SsaValueId> pending(&quota_);
    pending.push_back(value);
    while (!pending.empty()) {
      auto at = Root(pending.back());
      pending.pop_back();
      EquationStep();
      quota_.Step(1 + std::bit_width(owners_.size()));
      if (owners_.contains({guard.value, at.value})) continue;
      Require(owners_.size() < pair_limit_, Error::kResourceLimit);
      owners_.insert({guard.value, at.value});
      const auto& origin = ssa_->Values()[at.value].origin;
      if (std::holds_alternative<PhiValue>(origin)) {
        for (const auto& input : Inputs(at)) {
          EquationStep();
          pending.push_back(input.value);
        }
        continue;
      }
      const auto* site = std::get_if<InstructionValue>(&origin);
      Require(
          site && site->instruction.value % kSitesPerInstruction == 0 &&
              std::holds_alternative<SuperRange>(
                  instructions_[site->instruction.value / kSitesPerInstruction]
                      .operation),
          Error::kGuard);
      Require(proof_.bindings.size() < proof_.bindings.capacity(),
              Error::kResourceLimit);
      proof_.bindings.push_back(
          {guard, {site->instruction.value / kSitesPerInstruction}});
    }
  }
  void Equal(SsaValueId a, SsaValueId b) {
    pair_queue_.clear();
    const auto add = [&](SsaValueId x, SsaValueId y) {
      x = Root(x);
      y = Root(y);
      if (x == y) return;
      if (y < x) std::swap(x, y);
      EquationStep();
      quota_.Step(1 + std::bit_width(pairs_.size()));
      if (pairs_.contains({x.value, y.value})) return;
      Require(pairs_.size() < pair_limit_, Error::kResourceLimit);
      pairs_.insert({x.value, y.value});
      pair_queue_.push_back({x, y});
    };
    add(a, b);
    for (std::size_t cursor = 0; cursor < pair_queue_.size(); ++cursor) {
      EquationStep();
      const auto [x, y] = pair_queue_[cursor];
      const auto* p = std::get_if<PhiValue>(&ssa_->Values()[x.value].origin);
      const auto* r = std::get_if<PhiValue>(&ssa_->Values()[y.value].origin);
      Require(p || r, Error::kGuard);
      if (p && r && p->block == r->block) {
        const auto left = Inputs(x), right = Inputs(y);
        Require(left.size() == right.size(), Error::kGuard);
        for (std::size_t e = 0; e < left.size(); ++e) {
          EquationStep();
          Require(left[e].edge.index() == right[e].edge.index(), Error::kGuard);
          if (const auto* edge = std::get_if<SsaEdge>(&left[e].edge)) {
            const auto other = std::get<SsaEdge>(right[e].edge);
            Require(edge->source == other.source &&
                        edge->successor == other.successor,
                    Error::kGuard);
          }
          add(left[e].value, right[e].value);
        }
      } else if (p)
        for (const auto& input : Inputs(x)) add(input.value, y);
      else
        for (const auto& input : Inputs(y)) add(x, input.value);
    }
  }
  void SolvePaths() {
    std::vector<BlockId> queue;
    quota_.Reserve(queue, graph_.blocks.size() * 4);
    std::vector<bool> queued;
    quota_.Array(queued, graph_.blocks.size());
    incoming_[0] = {kUninitialized, static_cast<std::uint8_t>(Pending::kNone)};
    queue.push_back({0});
    queued[0] = true;
    for (std::size_t head = 0; head < queue.size(); ++head) {
      const auto b = queue[head];
      queued[b.value] = false;
      auto state = incoming_[b.value];
      const auto& block = graph_.blocks[b.value];
      for (std::size_t i = block.first_instruction;
           i < std::size_t(block.first_instruction) + block.instruction_count;
           ++i) {
        quota_.Step();
        at_[i] = state;
        if (!state.receiver) {
          state.pending = 0;
          continue;
        }
        const auto& instruction = instructions_[i];
        if (std::holds_alternative<SuperRange>(instruction.operation)) {
          Require(!state.receiver || state.pending == static_cast<std::uint8_t>(
                                                          Pending::kNone),
                  Error::kGuard);
          state.pending = static_cast<std::uint8_t>(Pending::kConstructed);
        } else if (const auto* check =
                       std::get_if<SuperCheck>(&instruction.operation)) {
          if (check->kind == SuperCheckKind::kRequireUninitialized) {
            Require(!state.receiver ||
                        state.pending ==
                            static_cast<std::uint8_t>(Pending::kConstructed),
                    Error::kGuard);
            state.receiver =
                (state.receiver & kUninitialized) ? kInitialized : 0;
            state.pending = static_cast<std::uint8_t>(Pending::kNone);
          } else if (check->kind == SuperCheckKind::kRequireInitialized) {
            Require(
                !state.receiver ||
                    state.pending == static_cast<std::uint8_t>(Pending::kNone),
                Error::kGuard);
            state.receiver &= kInitialized;
          }
        } else if (state.receiver &&
                   state.pending ==
                       static_cast<std::uint8_t>(Pending::kConstructed)) {
          Require(instruction.effects.normal == LanguageEffect::kNone &&
                      instruction.effects.exceptional ==
                          ExceptionalCompletion::kImpossible,
                  Error::kGuard);
          Require(
              !std::holds_alternative<ReturnOperation>(instruction.operation),
              Error::kGuard);
        }
      }
      for (std::size_t e = 0; e < block.successor_count; ++e) {
        quota_.Step();
        edges_[b.value * kMaxNormalSuccessors + e] = {state.receiver,
                                                      state.pending};
        const auto next = block.successors[e].target;
        auto& value = incoming_[next.value];
        const PathState joined{
            static_cast<std::uint8_t>(value.receiver | state.receiver),
            static_cast<std::uint8_t>(value.pending | state.pending)};
        if (joined.receiver == value.receiver &&
            joined.pending == value.pending)
          continue;
        value = joined;
        if (!queued[next.value]) {
          Require(queue.size() < queue.capacity(), Error::kResourceLimit);
          queue.push_back(next);
          queued[next.value] = true;
        }
      }
    }
  }
  void CheckPendingCycles() {
    // Moving a throwing rebind check across divergence is observable. A cycle
    // carrying an initialized receiver and an unbound construction therefore
    // cannot be absorbed by native super, even if it has a guarded exit.
    std::vector<std::uint32_t> degree;
    std::vector<BlockId> queue;
    quota_.Array(degree, graph_.blocks.size());
    quota_.Reserve(queue, graph_.blocks.size());
    const auto pending = [&](std::size_t b, std::size_t e) {
      const auto state = edges_[b * kMaxNormalSuccessors + e];
      return (state.receiver & kInitialized) &&
             state.pending == static_cast<std::uint8_t>(Pending::kConstructed);
    };
    for (std::size_t b = 0; b < graph_.blocks.size(); ++b)
      for (std::size_t e = 0; e < graph_.blocks[b].successor_count; ++e) {
        quota_.Step();
        if (pending(b, e))
          ++degree[graph_.blocks[b].successors[e].target.value];
      }
    for (std::uint32_t b = 0; b < degree.size(); ++b)
      if (!degree[b]) queue.push_back({b});
    for (std::size_t h = 0; h < queue.size(); ++h) {
      const auto b = queue[h].value;
      for (std::size_t e = 0; e < graph_.blocks[b].successor_count; ++e) {
        quota_.Step();
        if (pending(b, e)) {
          const auto target = graph_.blocks[b].successors[e].target;
          if (--degree[target.value] == 0) queue.push_back(target);
        }
      }
    }
    Require(queue.size() == graph_.blocks.size(), Error::kControlFlow);
  }
  struct Refinement {
    SsaValueId value;
    std::uint32_t begin, end;
  };
  bool Undefined(SsaValueId value) const {
    value = Root(value);
    const auto* origin =
        std::get_if<InstructionValue>(&ssa_->Values()[value.value].origin);
    if (!origin) return false;
    const auto* assign = std::get_if<ScalarAssignment>(
        &instructions_[origin->instruction.value / kSitesPerInstruction]
             .operation);
    const auto* scalar =
        assign ? std::get_if<ScalarValue>(&assign->expression) : nullptr;
    const auto* special = scalar ? std::get_if<SpecialValue>(scalar) : nullptr;
    return special && *special == SpecialValue::kUndefined;
  }
  void BuildRefinements() {
    // Edge splitting is for edge dominance, not a source/bytecode rewrite.
    // This admits critical edges and arbitrary block splitting equally.
    NormalFlow split;
    std::size_t count = graph_.blocks.size();
    for (const auto& block : graph_.blocks) count += block.successor_count;
    Require(count <= UINT32_MAX, Error::kResourceLimit);
    quota_.Array(split.blocks, count);
    quota_.Array(edge_blocks_, edges_.size());
    std::uint32_t cursor = graph_.blocks.size();
    for (std::uint32_t b = 0; b < graph_.blocks.size(); ++b) {
      split.blocks[b] = graph_.blocks[b];
      for (std::uint8_t e = 0; e < graph_.blocks[b].successor_count; ++e) {
        quota_.Step();
        const auto edge = graph_.blocks[b].successors[e];
        edge_blocks_[b * kMaxNormalSuccessors + e] = {cursor};
        split.blocks[b].successors[e].target = {cursor};
        split.blocks[cursor].successor_count = 1;
        split.blocks[cursor++].successors[0] = edge;
      }
    }
    auto built =
        BuildNormalDominators(split, {}, {quota_.left.bytes, quota_.left.work});
    if (const auto* error = std::get_if<DominatorError>(&built))
      throw *error == DominatorError::kResourceLimit ||
              *error == DominatorError::kWorkLimit
          ? Error::kResourceLimit
          : Error::kControlFlow;
    dominators_ = std::get<DominatorTree>(std::move(built));
    quota_.Admit(dominators_.work.allocated_bytes);
    quota_.Step(dominators_.work.steps);
    quota_.Reserve(refinements_, graph_.blocks.size());
    for (std::uint32_t b = 0; b < graph_.blocks.size(); ++b) {
      const auto& block = graph_.blocks[b];
      if (block.transfer != abc::ControlTransfer::kBranch) continue;
      const auto i = block.first_instruction + block.instruction_count - 1;
      const auto* branch =
          std::get_if<BranchOperation>(&instructions_[i].operation);
      if (!branch) continue;
      const auto reads = Reads({i * kSitesPerInstruction});
      if (reads.size() != 1) continue;
      const auto condition = Root(reads[0].value);
      const auto* origin = std::get_if<InstructionValue>(
          &ssa_->Values()[condition.value].origin);
      if (!origin) continue;
      const auto site = origin->instruction;
      const auto* assignment = std::get_if<ScalarAssignment>(
          &instructions_[site.value / kSitesPerInstruction].operation);
      const auto* comparison =
          assignment ? std::get_if<BinaryExpression>(&assignment->expression)
                     : nullptr;
      if (!comparison || (comparison->op != BinaryOperator::kStrictEqual &&
                          comparison->op != BinaryOperator::kStrictNotEqual))
        continue;
      const auto operands = Reads(site);
      if (operands.size() != 2) continue;
      std::optional<SsaValueId> compared;
      if (Undefined(operands[0].value))
        compared = Root(operands[1].value);
      else if (Undefined(operands[1].value))
        compared = Root(operands[0].value);
      if (!compared) continue;
      const bool wanted = comparison->op == BinaryOperator::kStrictNotEqual;
      bool taken_truth;
      switch (branch->predicate.op) {
        case UnaryOperator::kFalseOrZero:
        case UnaryOperator::kLogicalNot:
          taken_truth = false;
          break;
        case UnaryOperator::kTrueOrNonzeroNumber:
        case UnaryOperator::kToBoolean:
          taken_truth = true;
          break;
        default:
          continue;
      }
      for (std::uint8_t e = 0; e < block.successor_count; ++e) {
        quota_.Step();
        const bool truth = block.successors[e].kind == FlowEdgeKind::kTaken
                               ? taken_truth
                               : !taken_truth;
        if (truth != wanted) continue;
        const auto& interval =
            dominators_.nodes[edge_blocks_[b * kMaxNormalSuccessors + e].value];
        if (interval.begin)
          refinements_.push_back({*compared, interval.begin, interval.end});
      }
    }
    quota_.Step(refinements_.size() *
                (1 + std::bit_width(refinements_.size())));
    std::sort(refinements_.begin(), refinements_.end(),
              [](const auto& a, const auto& b) {
                return a.value != b.value ? a.value < b.value
                                          : a.begin < b.begin;
              });
    std::size_t used = 0;
    for (const auto item : refinements_) {
      if (used && refinements_[used - 1].value == item.value &&
          item.begin <= refinements_[used - 1].end)
        refinements_[used - 1].end =
            std::max(refinements_[used - 1].end, item.end);
      else
        refinements_[used++] = item;
    }
    refinements_.resize(used);
  }
  bool Defined(SsaValueId value, BlockId use) {
    // Batch AND-reachability over value equations and edge refinements. A
    // cyclic phi contributes no new origin; every external contribution must
    // still exclude undefined. Shared memoization prevents per-return walks.
    std::pmr::vector<std::pair<SsaValueId, BlockId>> pending(&quota_);
    pending.push_back({value, use});
    while (!pending.empty()) {
      auto [at, where] = pending.back();
      pending.pop_back();
      at = Root(at);
      EquationStep();
      quota_.Step(1 + std::bit_width(defined_.size()));
      if (defined_.contains({at.value, where.value})) continue;
      Require(defined_.size() < pair_limit_, Error::kResourceLimit);
      defined_.insert({at.value, where.value});
      const auto position = dominators_.nodes[where.value].begin;
      const Refinement key{at, position, position};
      quota_.Step(1 + std::bit_width(refinements_.size()));
      const auto after = std::upper_bound(
          refinements_.begin(), refinements_.end(), key,
          [](const auto& a, const auto& b) {
            return a.value != b.value ? a.value < b.value : a.begin < b.begin;
          });
      if (after != refinements_.begin()) {
        const auto& item = *std::prev(after);
        if (item.value == at && item.begin <= position && position < item.end)
          continue;
      }
      const auto& origin = ssa_->Values()[at.value].origin;
      if (const auto* site = std::get_if<InstructionValue>(&origin)) {
        const auto& op =
            instructions_[site->instruction.value / kSitesPerInstruction]
                .operation;
        if (std::holds_alternative<SuperRange>(op) ||
            std::holds_alternative<EmptyObjectCreation>(op) ||
            std::holds_alternative<FunctionCreation>(op) ||
            std::holds_alternative<StringLoad>(op))
          continue;
        if (const auto* scalar = std::get_if<ScalarAssignment>(&op))
          if (const auto* literal =
                  std::get_if<ScalarValue>(&scalar->expression)) {
            if (!std::holds_alternative<Accumulator>(*literal) &&
                !std::holds_alternative<abc::RegisterId>(*literal) &&
                (!std::holds_alternative<SpecialValue>(*literal) ||
                 std::get<SpecialValue>(*literal) != SpecialValue::kUndefined))
              continue;
          }
      }
      if (std::holds_alternative<PhiValue>(origin)) {
        for (const auto& input : Inputs(at)) {
          EquationStep();
          const auto* edge = std::get_if<SsaEdge>(&input.edge);
          if (!edge) return false;
          const auto e =
              edge->source.value * kMaxNormalSuccessors + edge->successor;
          if (edges_[e].receiver & kUninitialized)
            pending.push_back({input.value, edge_blocks_[e]});
        }
        continue;
      }
      return false;
    }
    return true;
  }
  const abc::MethodBody& body_;
  const abc::FrameLayout& frame_;
  abc::ProfileId profile_;
  Quota& quota_;
  NormalFlow graph_;
  std::vector<SemanticInstruction> instructions_;
  std::vector<RegisterSite> sites_;
  std::optional<RegisterSsa> ssa_;
  std::optional<SsaValueId> entry_this_;
  std::vector<SsaValueId> roots_;
  std::vector<std::optional<SsaValueId>> copy_;
  std::vector<BlockId> blocks_;
  std::vector<PathState> incoming_, at_;
  std::vector<EdgeState> edges_;
  std::vector<std::optional<InstructionId>> checks_;
  DominatorTree dominators_;
  std::vector<Refinement> refinements_;
  std::vector<BlockId> edge_blocks_;
  std::pmr::set<std::pair<std::uint32_t, std::uint32_t>> pairs_;
  std::pmr::set<std::pair<std::uint32_t, std::uint32_t>> defined_, owners_;
  std::uint64_t equations_ = 0;
  std::vector<std::pair<SsaValueId, SsaValueId>> pair_queue_;
  std::size_t pair_limit_ = 0;
  RangeDerivedConstructor proof_;
};
}  // namespace
RangeDerivedConstructorResult ProveRangeConstructorProtocol(
    const abc::MethodBody& body, const abc::FrameLayout& frame,
    DerivedConstructorBudget budget, abc::ProfileId profile) {
  try {
    Quota quota(budget);
    return Protocol(body, frame, profile, quota).Run();
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
