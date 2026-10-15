#include "abcd/analysis/await_catch_flow.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/exception_dispatch.h"
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
constexpr auto kPhases = RawEventGraph::kPhases;
struct Summary {
  std::optional<abc::CodeOffset> read, unknown, cover, terminal;
  bool operator==(const Summary&) const = default;
};
CatchFlowFailure CatchFailure(const RawEventFailure& failure) {
  CatchFlowError kind;
  switch (failure.kind) {
    case RawEventError::kRegion: kind = CatchFlowError::kRegion; break;
    case RawEventError::kTarget: kind = CatchFlowError::kTarget; break;
    case RawEventError::kCorrespondence: kind = CatchFlowError::kCorrespondence; break;
    case RawEventError::kResourceLimit: kind = CatchFlowError::kResourceLimit; break;
    case RawEventError::kWorkLimit: kind = CatchFlowError::kWorkLimit; break;
  }
  return {kind, failure.pc};
}
class Flow {
 public:
  Flow(const abc::MethodBody& body, std::span<const AwaitRegion> regions,
       abc::ProfileId profile, std::uint64_t frame, abc::CodeOffset completion,
       SemanticBudget budget)
      : memory_(budget), graph_(body, memory_), queries_(memory_.Resource()) {
    const auto count = body.instructions.size();
    if (count > (UINT32_MAX - 1) / kPhases) Fail(CatchFlowError::kResourceLimit);
    const auto events = count * kPhases;
    if (events > UINT64_MAX / kWorkPerEvent / (1 + std::bit_width(events)))
      Fail(CatchFlowError::kResourceLimit);
    memory_.LimitWork(kWorkPerEvent * events * (1 + std::bit_width(events)));
    // Upstream routines precharge their buffers. Retain only surviving payload;
    // scratch observes overlap with all arrays already alive in this analysis.
    auto found = FindAwaitRegions(body, profile, frame, memory_.Available());
    if (auto* e = std::get_if<AwaitRegionFailure>(&found))
      Fail(e->kind == AwaitRegionError::kResourceLimit ? CatchFlowError::kResourceLimit : CatchFlowError::kProtocol, e->pc);
    const auto& verified = std::get<std::vector<AwaitRegion>>(found);
    memory_.Retain(verified.capacity() * sizeof(AwaitRegion));
    if (!std::equal(regions.begin(), regions.end(), verified.begin(), verified.end()))
      Fail(CatchFlowError::kProtocol);
    const auto dispatch = graph_.Select(profile);
    graph_.Allocate();
    // A single sorted index of protocol membership, no per-instruction region walk.
    std::pmr::vector<std::optional<std::size_t>> owner(count, memory_.Resource());
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
    struct Extension final : RawEventExtension {
      Extension(const abc::MethodBody& body, std::span<const std::optional<std::size_t>> owner,
                std::uint64_t frame) : body(body), owner(owner), frame(frame) {}
      const abc::MethodBody& body;
      std::span<const std::optional<std::size_t>> owner;
      std::uint64_t frame;
      std::optional<RawEventExtensionEffect> Effect(std::size_t i) const override {
        if (!owner[i]) return {};
        RawEventExtensionEffect effect;
        const auto coroutine = LiftCoroutineInstruction(body.instructions[i].decoded, frame);
        if (const auto* transfer = std::get_if<CoroutineTransfer>(&coroutine)) {
          effect.registers.reads[0] = transfer->state;
          effect.registers.reads[1] = transfer->value;
          effect.registers.read_count = 2;
          effect.registers.write = Accumulator{};
          if (transfer->kind == CoroutineTransferKind::kAwait)
            effect.exceptional = ExceptionalCompletion::kMayThrow;
          else if (transfer->kind != CoroutineTransferKind::kSuspend) effect.unknown = true;
        } else if (const auto* read = std::get_if<CoroutineRead>(&coroutine)) {
          effect.resume = read->kind == CoroutineReadKind::kResumeValue;
          effect.registers.reads[0] = read->state;
          effect.registers.read_count = 1;
          effect.registers.write = Accumulator{};
        } else effect.unknown = true;
        return effect;
      }
      bool Resume(std::size_t i) const override { return owner[i].has_value(); }
    } extension{body, owner, frame};
    graph_.Connect(dispatch, profile, frame, &extension);
    // Conservative retained accounting lasts through construction; no credit
    // is taken while either upstream vector is still alive.
  }
  void Step(std::uint64_t n = 1) { memory_.Step(n); }
  [[noreturn]] static void Fail(CatchFlowError kind, std::optional<abc::CodeOffset> pc = {}) {
    throw CatchFlowFailure{kind, pc};
  }
  std::size_t Site(abc::CodeOffset pc) { return graph_.Site(pc); }
  abc::CodeOffset Pc(std::uint32_t node) const { return graph_.Pc(node); }
  Summary Boundary(CatchFlowState state) {
    Step(); if (state.node >= graph_.Nodes().size()) Fail(CatchFlowError::kCorrespondence);
    if (graph_.Nodes()[state.node].phase == CatchFlowPhase::kCallerExit) return {{}, {}, {}, Pc(state.node)};
    const auto& n = graph_.Nodes()[state.node]; Summary s;
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
    std::pmr::vector<Summary> summaries(states.size(), memory_.Resource());
    // Reverse closure edges; every state has at most three outgoing edges.
    struct Arc { std::uint32_t target, predecessor; };
    std::pmr::vector<Arc> reverse(memory_.Resource());
    std::pmr::vector<std::uint32_t> queue(memory_.Resource());
    std::pmr::vector<std::uint8_t> queued(states.size(), memory_.Resource());
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
      else for (std::size_t j = 0; j < graph_.Nodes()[states[i].node].count; ++j)
        reverse.push_back({find({graph_.Nodes()[states[i].node].next[j], states[i].slot}), i});
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
    std::pmr::map<CatchFlowState, std::uint32_t> index(memory_.Resource());
    std::pmr::vector<CatchFlowState> closure(memory_.Resource());
    const auto add = [&](CatchFlowState s) {
      Step(); if (index.contains(s)) return;
      if (closure.size() == graph_.Nodes().size() * kStatesPerEvent) Fail(CatchFlowError::kResourceLimit);
      index.emplace(s, static_cast<std::uint32_t>(closure.size())); closure.push_back(s);
    };
    for (const auto& q : queries_) add({static_cast<std::uint32_t>(Site(q.query.handler) * kPhases + 2), q.query.slot});
    for (std::size_t cursor = 0; cursor < closure.size(); ++cursor) {
      const auto s = closure[cursor];
      if (Stop(Boundary(s))) continue;
      for (std::size_t j = 0; j < graph_.Nodes()[s.node].count; ++j) add({graph_.Nodes()[s.node].next[j], s.slot});
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
    result.peak_bytes = memory_.Peak(); result.work = memory_.Work();
    return result;
  }
  template<class T> void Copy(std::vector<T>& out, std::span<const T> in) {
    memory_.Retain(in.size_bytes()); out.reserve(in.size());
    if (out.capacity() != in.size()) Fail(CatchFlowError::kResourceLimit);
    out.assign(in.begin(), in.end());
  }
  CatchFlowCost Verify(const AwaitCatchFlowProof& proof) {
    if (proof.states.size() > graph_.Nodes().size() * kStatesPerEvent) Fail(CatchFlowError::kResourceLimit);
    for (std::size_t i = 0; i < proof.states.size(); ++i) {
      Step(); if ((i && !(proof.states[i - 1] < proof.states[i])) || proof.states[i].node >= graph_.Nodes().size())
        Fail(CatchFlowError::kCorrespondence);
    }
    // Check the supplied closure from its independently derived roots, rather
    // than trusting producer membership or a caller-provided consumed mask.
    std::pmr::vector<std::uint8_t> reached(proof.states.size(), memory_.Resource());
    std::pmr::vector<std::size_t> pending(memory_.Resource());
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
      for (std::size_t j = 0; j < graph_.Nodes()[s.node].count; ++j) visit({graph_.Nodes()[s.node].next[j], s.slot});
    }
    if (pending.size() != proof.states.size()) Fail(CatchFlowError::kCorrespondence);
    const auto expected = Result(proof.states);
    if (expected.queries != proof.queries) Fail(CatchFlowError::kCorrespondence);
    return {memory_.Peak(), memory_.Work()};
  }
 private:
  RawEventArena memory_;
  RawEventGraph graph_;
  std::pmr::vector<CatchFlowEvidence> queries_;
};
}
std::variant<AwaitCatchFlowProof, CatchFlowFailure> ProveAwaitCatchFlow(
    const abc::MethodBody& body, std::span<const AwaitRegion> regions,
    abc::ProfileId profile, std::uint64_t frame, abc::CodeOffset completion, SemanticBudget budget) {
  try { return Flow(body, regions, profile, frame, completion, budget).Prove(); }
  catch (const CatchFlowFailure& failure) { return failure; }
  catch (const RawEventFailure& failure) { return CatchFailure(failure); }
  catch (const std::bad_alloc&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
  catch (const std::length_error&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
}
std::variant<CatchFlowCost, CatchFlowFailure> VerifyAwaitCatchFlow(
    const abc::MethodBody& body, std::span<const AwaitRegion> regions,
    abc::ProfileId profile, std::uint64_t frame, abc::CodeOffset completion,
    const AwaitCatchFlowProof& proof, SemanticBudget budget) {
  try { return Flow(body, regions, profile, frame, completion, budget).Verify(proof); }
  catch (const CatchFlowFailure& failure) { return failure; }
  catch (const RawEventFailure& failure) { return CatchFailure(failure); }
  catch (const std::bad_alloc&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
  catch (const std::length_error&) { return CatchFlowFailure{CatchFlowError::kResourceLimit, {}}; }
}
} // namespace abcd::analysis
