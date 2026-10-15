#include "abcd/analysis/await_catch_flow.h"
#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/exception_dispatch.h"
#include "idf_scratch.h"
#include <algorithm>
#include <bit>
#include <map>
#include <memory_resource>
#include <stdexcept>

namespace abcd::analysis {
namespace {
// Resource admission, not a recovery shape restriction: sparse register/event
// products may be dense. Stop before expansion exceeds linear storage/work.
constexpr std::size_t kStatesPerEvent = 8;
constexpr std::uint64_t kWorkPerEvent = 64;
constexpr std::uint32_t kPhases = 4;
struct Node {
  std::array<std::uint32_t, 3> next{};
  std::uint8_t count = 0;
  CatchFlowPhase phase = CatchFlowPhase::kBefore;
  RegisterEffect registers;
  bool unknown = false;
};
struct Summary {
  std::optional<abc::CodeOffset> read, unknown, cover, terminal;
  bool operator==(const Summary&) const = default;
};
class Flow {
 public:
  Flow(const abc::MethodBody& body, std::span<const AwaitRegion> regions,
       abc::ProfileId profile, std::uint64_t frame, abc::CodeOffset completion,
       SemanticBudget budget)
      : body_(body), memory_(budget.bytes), nodes_(&memory_), queries_(&memory_),
        work_(budget.work), initial_(budget.work) {
    const auto count = body.instructions.size();
    if (count > (UINT32_MAX - 1) / kPhases) Fail(CatchFlowError::kResourceLimit);
    const auto events = count * kPhases;
    if (events > UINT64_MAX / kWorkPerEvent / (1 + std::bit_width(events)))
      Fail(CatchFlowError::kResourceLimit);
    work_ = std::min(work_, kWorkPerEvent * events * (1 + std::bit_width(events)));
    initial_ = work_;
    // Upstream routines precharge their buffers. Retain only surviving payload;
    // scratch observes overlap with all arrays already alive in this analysis.
    auto found = FindAwaitRegions(body, profile, frame, memory_.Available());
    if (auto* e = std::get_if<AwaitRegionFailure>(&found))
      Fail(e->kind == AwaitRegionError::kResourceLimit ? CatchFlowError::kResourceLimit : CatchFlowError::kProtocol, e->pc);
    const auto& verified = std::get<std::vector<AwaitRegion>>(found);
    memory_.Retain(verified.capacity() * sizeof(AwaitRegion));
    if (!std::equal(regions.begin(), regions.end(), verified.begin(), verified.end()))
      Fail(CatchFlowError::kProtocol);
    auto selected = BuildExceptionDispatch(body, profile, {memory_.Available()});
    if (auto* e = std::get_if<DispatchFailure>(&selected))
      Fail(e->kind == DispatchError::kResourceLimit ? CatchFlowError::kResourceLimit : CatchFlowError::kRegion,
           e->kind == DispatchError::kInvalidBoundary ? std::optional{e->offset} : std::nullopt);
    const auto& dispatch = std::get<ExceptionDispatch>(selected);
    memory_.Retain(dispatch.spans.capacity() * sizeof(DispatchSpan));
    nodes_.resize(events);
    // A single sorted index of protocol membership, no per-instruction region walk.
    std::pmr::vector<std::optional<std::size_t>> owner(count, &memory_);
    for (std::size_t r = 0; r < regions.size(); ++r) {
      Step(); const auto& region = regions[r];
      if (!region.instructions) Fail(CatchFlowError::kProtocol, region.await);
      for (const auto pc : *region.instructions) {
        Step(); const auto i = Site(pc);
        if (owner[i]) Fail(CatchFlowError::kProtocol, pc);
        owner[i] = r;
      }
      const auto target = dispatch.Find(region.await); Step(1 + std::bit_width(dispatch.spans.size()));
      if (target != dispatch.Find(region.reject)) Fail(CatchFlowError::kProtocol, region.await);
      if (target && target->handler_pc != completion) {
        queries_.push_back({{region.await, region.result_store, target->handler_pc, region.result, CatchFlowRole::kResult}});
        queries_.push_back({{region.await, region.mode_store, target->handler_pc, region.resume_mode, CatchFlowRole::kResumeMode}});
      }
    }
    for (std::size_t i = 0; i < count; ++i) {
      Step(); const auto& site = body.instructions[i];
      auto& before = nodes_[i * kPhases];
      auto& commit = nodes_[i * kPhases + 1]; commit.phase = CatchFlowPhase::kCommit;
      auto& caught = nodes_[i * kPhases + 2]; caught.phase = CatchFlowPhase::kCatch;
      Add(caught, static_cast<std::uint32_t>(i * kPhases));
      nodes_[i * kPhases + 3].phase = CatchFlowPhase::kCallerExit;
      const auto lifted = LiftSemanticInstruction(site, profile, frame);
      auto exceptional = ExceptionalCompletion::kImpossible;
      if (const auto* semantic = std::get_if<SemanticInstruction>(&lifted)) {
        before.registers = semantic->effects.normal_registers;
        exceptional = semantic->effects.exceptional;
      } else if (owner[i]) {
        const auto coroutine = LiftCoroutineInstruction(site.decoded, frame);
        if (const auto* transfer = std::get_if<CoroutineTransfer>(&coroutine)) {
          before.registers.reads[0] = transfer->state;
          before.registers.reads[1] = transfer->value;
          before.registers.read_count = 2;
          before.registers.write = Accumulator{};
          if (transfer->kind == CoroutineTransferKind::kAwait) exceptional = ExceptionalCompletion::kMayThrow;
          else if (transfer->kind != CoroutineTransferKind::kSuspend) before.unknown = true;
        } else if (const auto* read = std::get_if<CoroutineRead>(&coroutine)) {
          if (read->kind == CoroutineReadKind::kResumeValue) before.phase = CatchFlowPhase::kResume;
          before.registers.reads[0] = read->state; before.registers.read_count = 1;
          before.registers.write = Accumulator{};
        } else before.unknown = true;
      } else before.unknown = true;
      // Inputs belong to Before; the destination exists only on success.
      commit.registers.write = before.registers.write; before.registers.write.reset();
      if (exceptional != ExceptionalCompletion::kAlwaysThrow) Add(before, static_cast<std::uint32_t>(i * kPhases + 1));
      if (exceptional != ExceptionalCompletion::kImpossible) {
        Step(1 + std::bit_width(dispatch.spans.size())); const auto target = dispatch.Find(site.offset);
        Add(before, target ? static_cast<std::uint32_t>(Site(target->handler_pc) * kPhases + 2) : Exit(i));
      }
      const auto transfer = abc::GetControlTransfer(site.decoded.opcode);
      if (!transfer) { before.unknown = true; continue; }
      const auto next = [&] {
        if (i + 1 >= count) Fail(CatchFlowError::kTarget, site.offset);
        Add(commit, static_cast<std::uint32_t>((i + 1) * kPhases));
      };
      switch (*transfer) {
        case abc::ControlTransfer::kFallthrough: next(); break;
        case abc::ControlTransfer::kJump:
        case abc::ControlTransfer::kBranch:
          if (!site.branch_target) Fail(CatchFlowError::kTarget, site.offset);
          Add(commit, static_cast<std::uint32_t>(Site(*site.branch_target) * kPhases));
          if (*transfer == abc::ControlTransfer::kBranch) next();
          break;
        case abc::ControlTransfer::kReturn: Add(commit, Exit(i)); break;
        case abc::ControlTransfer::kThrow: break;
        case abc::ControlTransfer::kSuspend:
          if (!owner[i]) before.unknown = true;
          else { next(); Add(commit, Exit(i)); }
          break;
      }
    }
    // Conservative retained accounting lasts through construction; no credit
    // is taken while either upstream vector is still alive.
  }
  void Step(std::uint64_t n = 1) {
    if (n > work_) Fail(CatchFlowError::kWorkLimit);
    work_ -= n;
  }
  [[noreturn]] static void Fail(CatchFlowError kind, std::optional<abc::CodeOffset> pc = {}) {
    throw CatchFlowFailure{kind, pc};
  }
  std::uint32_t Exit(std::size_t site) const { return static_cast<std::uint32_t>(site * kPhases + 3); }
  std::size_t Site(abc::CodeOffset pc) {
    const auto at = std::lower_bound(body_.instructions.begin(), body_.instructions.end(), pc,
        [&](const auto& i, auto p) { Step(); return i.offset < p; });
    if (at == body_.instructions.end() || at->offset != pc) Fail(CatchFlowError::kTarget, pc);
    return static_cast<std::size_t>(at - body_.instructions.begin());
  }
  void Add(Node& n, std::uint32_t id) { Step(); if (n.count == n.next.size()) Fail(CatchFlowError::kCorrespondence); n.next[n.count++] = id; }
  abc::CodeOffset Pc(std::uint32_t node) const { return body_.instructions[node / kPhases].offset; }
  Summary Boundary(CatchFlowState state) {
    Step(); if (state.node >= nodes_.size()) Fail(CatchFlowError::kCorrespondence);
    if (nodes_[state.node].phase == CatchFlowPhase::kCallerExit) return {{}, {}, {}, Pc(state.node)};
    const auto& n = nodes_[state.node]; Summary s;
    const auto pc = Pc(state.node);
    const auto& e = n.registers;
    for (std::size_t r = 0; r < e.read_count; ++r) {
      if (const auto* reg = std::get_if<abc::RegisterId>(&e.reads[r]); reg && *reg == state.slot) s.read = pc;
    }
    if (e.range && state.slot.value >= e.range->first.value &&
        std::uint64_t(state.slot.value) - e.range->first.value < e.range->count) s.read = pc;
    if (n.unknown) s.unknown = pc;
    if (e.write) if (const auto* reg = std::get_if<abc::RegisterId>(&*e.write); reg && *reg == state.slot) s.cover = pc;
    return s;
  }
  static bool Stop(const Summary& s) { return s.read || s.unknown || s.cover || s.terminal; }
  CatchFlowEvidence Evidence(const CatchFlowQuery& q, const Summary& s) const {
    auto kind = s.read ? CatchFlowOutcome::kObservedRead : s.unknown ? CatchFlowOutcome::kUnknownEffect :
        s.cover ? (s.terminal ? CatchFlowOutcome::kCoveredAndTerminal : CatchFlowOutcome::kCovered) :
        s.terminal ? CatchFlowOutcome::kTerminal : CatchFlowOutcome::kUnobserved;
    return {q, kind, s.read, s.unknown, s.cover, s.terminal};
  }
  std::pmr::vector<Summary> Summaries(std::span<const CatchFlowState> states) {
    std::pmr::vector<Summary> summaries(states.size(), &memory_);
    // Reverse closure edges; every state has at most three outgoing edges.
    struct Arc { std::uint32_t target, predecessor; };
    std::pmr::vector<Arc> reverse(&memory_);
    std::pmr::vector<std::uint32_t> queue(&memory_);
    std::pmr::vector<std::uint8_t> queued(states.size(), &memory_);
    const auto find = [&](CatchFlowState key) {
      const auto at = std::lower_bound(states.begin(), states.end(), key,
          [&](auto a, auto b) { Step(); return a < b; });
      if (at == states.end() || *at != key) Fail(CatchFlowError::kCorrespondence);
      return static_cast<std::uint32_t>(at - states.begin());
    };
    const auto enqueue = [&](std::uint32_t id) { if (!queued[id]) { queued[id] = 1; queue.push_back(id); } };
    for (std::uint32_t i = 0; i < states.size(); ++i) {
      summaries[i] = Boundary(states[i]);
      if (Stop(summaries[i])) enqueue(i);
      else for (std::size_t j = 0; j < nodes_[states[i].node].count; ++j)
        reverse.push_back({find({nodes_[states[i].node].next[j], states[i].slot}), i});
    }
    std::sort(reverse.begin(), reverse.end(), [&](auto a, auto b) { Step(); return a.target < b.target; });
    // Propagate only presence, never repeatedly improving a witness in a cycle.
    // Deterministic node order selects a real reachable witness for each bit.
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
      Step(); const auto id = queue[cursor]; queued[id] = 0;
      const auto s = summaries[id];
      auto it = std::lower_bound(reverse.begin(), reverse.end(), id,
          [&](auto a, auto b) { Step(); return a.target < b; });
      while (it != reverse.end() && it->target == id) {
        Step(); auto& p = summaries[it->predecessor]; bool changed = false;
        const auto merge = [&](auto& to, const auto& from) { if (!to && from) { to = from; changed = true; } };
        merge(p.read, s.read); merge(p.unknown, s.unknown); merge(p.cover, s.cover); merge(p.terminal, s.terminal);
        if (changed) enqueue(it->predecessor);
        ++it;
      }
    }
    return summaries;
  }
  AwaitCatchFlowProof Prove() {
    std::pmr::map<CatchFlowState, std::uint32_t> index(&memory_);
    std::pmr::vector<CatchFlowState> closure(&memory_);
    const auto add = [&](CatchFlowState s) {
      Step(); if (index.contains(s)) return;
      if (closure.size() == nodes_.size() * kStatesPerEvent) Fail(CatchFlowError::kResourceLimit);
      index.emplace(s, static_cast<std::uint32_t>(closure.size())); closure.push_back(s);
    };
    for (const auto& q : queries_) add({static_cast<std::uint32_t>(Site(q.query.handler) * kPhases + 2), q.query.slot});
    for (std::size_t cursor = 0; cursor < closure.size(); ++cursor) {
      const auto s = closure[cursor];
      if (Stop(Boundary(s))) continue;
      for (std::size_t j = 0; j < nodes_[s.node].count; ++j) add({nodes_[s.node].next[j], s.slot});
    }
    std::sort(closure.begin(), closure.end(), [&](auto a, auto b) { Step(); return a < b; });
    return Result(closure);
  }
  AwaitCatchFlowProof Result(std::span<const CatchFlowState> states) {
    const auto summaries = Summaries(states);
    AwaitCatchFlowProof result;
    Copy(result.states, states);
    memory_.Retain(queries_.size() * sizeof(CatchFlowEvidence)); result.queries.reserve(queries_.size());
    if (result.queries.capacity() != queries_.size()) Fail(CatchFlowError::kResourceLimit);
    for (const auto& q : queries_) {
      const CatchFlowState root{static_cast<std::uint32_t>(Site(q.query.handler) * kPhases + 2), q.query.slot};
      const auto at = std::lower_bound(states.begin(), states.end(), root,
          [&](auto a, auto b) { Step(); return a < b; });
      if (at == states.end() || *at != root) Fail(CatchFlowError::kCorrespondence);
      result.queries.push_back(Evidence(q.query, summaries[at - states.begin()]));
    }
    result.peak_bytes = memory_.Peak(); result.work = initial_ - work_;
    return result;
  }
  template<class T> void Copy(std::vector<T>& out, std::span<const T> in) {
    memory_.Retain(in.size_bytes()); out.reserve(in.size());
    if (out.capacity() != in.size()) Fail(CatchFlowError::kResourceLimit);
    out.assign(in.begin(), in.end());
  }
  CatchFlowCost Verify(const AwaitCatchFlowProof& proof) {
    if (proof.states.size() > nodes_.size() * kStatesPerEvent) Fail(CatchFlowError::kResourceLimit);
    for (std::size_t i = 0; i < proof.states.size(); ++i) {
      Step(); if ((i && !(proof.states[i - 1] < proof.states[i])) || proof.states[i].node >= nodes_.size())
        Fail(CatchFlowError::kCorrespondence);
    }
    // Check the supplied closure from its independently derived roots, rather
    // than trusting producer membership or a caller-provided consumed mask.
    std::pmr::vector<std::uint8_t> reached(proof.states.size(), &memory_);
    std::pmr::vector<std::size_t> pending(&memory_);
    const auto visit = [&](CatchFlowState s) {
      const auto at = std::lower_bound(proof.states.begin(), proof.states.end(), s,
          [&](auto a, auto b) { Step(); return a < b; });
      if (at == proof.states.end() || *at != s) Fail(CatchFlowError::kCorrespondence);
      const auto i = static_cast<std::size_t>(at - proof.states.begin());
      if (!reached[i]) { reached[i] = 1; pending.push_back(i); }
    };
    for (const auto& q : queries_) visit({static_cast<std::uint32_t>(Site(q.query.handler) * kPhases + 2), q.query.slot});
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
      const auto s = proof.states[pending[cursor]];
      if (Stop(Boundary(s))) continue;
      for (std::size_t j = 0; j < nodes_[s.node].count; ++j) visit({nodes_[s.node].next[j], s.slot});
    }
    if (pending.size() != proof.states.size()) Fail(CatchFlowError::kCorrespondence);
    const auto expected = Result(proof.states);
    if (expected.queries != proof.queries) Fail(CatchFlowError::kCorrespondence);
    return {memory_.Peak(), initial_ - work_};
  }
 private:
  const abc::MethodBody& body_;
  detail::IdfScratch memory_;
  std::pmr::vector<Node> nodes_;
  std::pmr::vector<CatchFlowEvidence> queries_;
  std::uint64_t work_, initial_;
};
}
std::variant<AwaitCatchFlowProof, CatchFlowFailure> ProveAwaitCatchFlow(
    const abc::MethodBody& body, std::span<const AwaitRegion> regions,
    abc::ProfileId profile, std::uint64_t frame, abc::CodeOffset completion, SemanticBudget budget) {
  try { return Flow(body, regions, profile, frame, completion, budget).Prove(); }
  catch (const CatchFlowFailure& failure) { return failure; }
  catch (const std::bad_alloc&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
  catch (const std::length_error&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
}
std::variant<CatchFlowCost, CatchFlowFailure> VerifyAwaitCatchFlow(
    const abc::MethodBody& body, std::span<const AwaitRegion> regions,
    abc::ProfileId profile, std::uint64_t frame, abc::CodeOffset completion,
    const AwaitCatchFlowProof& proof, SemanticBudget budget) {
  try { return Flow(body, regions, profile, frame, completion, budget).Verify(proof); }
  catch (const CatchFlowFailure& failure) { return failure; }
  catch (const std::bad_alloc&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
  catch (const std::length_error&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
}
} // namespace abcd::analysis
