#include "abcd/analysis/raw_event_graph.h"
#include "abcd/abc/control_transfer.h"
#include <algorithm>
#include <bit>

namespace abcd::analysis {
namespace {
[[noreturn]] void Fail(RawEventError kind, std::optional<abc::CodeOffset> pc = {}) {
  throw RawEventFailure{kind, pc};
}
std::uint32_t Exit(std::size_t site) {
  return static_cast<std::uint32_t>(site * RawEventGraph::kPhases + 3);
}
}
void RawEventArena::LimitWork(std::uint64_t limit) {
  if (left_ != initial_) Fail(RawEventError::kCorrespondence);
  left_ = std::min(left_, limit); initial_ = left_;
}
void RawEventArena::Step(std::uint64_t units) {
  if (units > left_) Fail(RawEventError::kWorkLimit);
  left_ -= units;
}
ExceptionDispatch RawEventGraph::Select(abc::ProfileId profile) {
  auto selected = BuildExceptionDispatch(body_, profile, {arena_.Available()});
  if (auto* e = std::get_if<DispatchFailure>(&selected))
    Fail(e->kind == DispatchError::kResourceLimit ? RawEventError::kResourceLimit : RawEventError::kRegion,
        e->kind == DispatchError::kInvalidBoundary ? std::optional{e->offset} : std::nullopt);
  auto dispatch = std::get<ExceptionDispatch>(std::move(selected));
  arena_.Retain(dispatch.spans.capacity() * sizeof(DispatchSpan));
  return dispatch;
}
void RawEventGraph::Allocate() {
  if (body_.instructions.size() > (UINT32_MAX - 1) / kPhases)
    Fail(RawEventError::kResourceLimit);
  if (state_ != State::kEmpty) Fail(RawEventError::kCorrespondence);
  nodes_.resize(body_.instructions.size() * kPhases);
  state_ = State::kAllocated;
}
std::span<const RawEventNode> RawEventGraph::Nodes() const {
  if (state_ != State::kReady) Fail(RawEventError::kCorrespondence);
  return nodes_;
}
std::size_t RawEventGraph::Site(abc::CodeOffset pc) {
  const auto at = std::lower_bound(body_.instructions.begin(), body_.instructions.end(), pc,
      [&](const auto& i, auto p) { arena_.Step(); return i.offset < p; });
  if (at == body_.instructions.end() || at->offset != pc) Fail(RawEventError::kTarget, pc);
  return static_cast<std::size_t>(at - body_.instructions.begin());
}
abc::CodeOffset RawEventGraph::Pc(std::uint32_t node) const {
  if (node >= nodes_.size()) Fail(RawEventError::kCorrespondence);
  return body_.instructions[node / kPhases].offset;
}
void RawEventGraph::Add(RawEventNode& node, std::uint32_t target) {
  arena_.Step();
  if (node.count == node.next.size()) Fail(RawEventError::kCorrespondence);
  node.next[node.count++] = target;
}
void RawEventGraph::Connect(const ExceptionDispatch& dispatch, abc::ProfileId profile,
    std::uint64_t frame, const RawEventExtension* extension, RawEventSemantics* semantics) {
  const auto count = body_.instructions.size();
  // This is a one-shot builder; a partially failed graph is never consumable.
  if (state_ != State::kAllocated || nodes_.size() != count * kPhases) Fail(RawEventError::kCorrespondence);
  state_ = State::kConnecting;
  for (std::size_t i = 0; i < count; ++i) {
    arena_.Step(); const auto& site = body_.instructions[i];
    auto& before = nodes_[i * kPhases];
    auto& commit = nodes_[i * kPhases + 1]; commit.phase = RawEventPhase::kCommit;
    auto& caught = nodes_[i * kPhases + 2]; caught.phase = RawEventPhase::kCatch;
    // Catch's acc comes from the selected exception, not the failed normal
    // destination. Await liveness queries only register slots, never acc.
    caught.registers.write = Accumulator{};
    Add(caught, static_cast<std::uint32_t>(i * kPhases));
    nodes_[i * kPhases + 3].phase = RawEventPhase::kCallerExit;
    const auto lifted = semantics ? semantics->Lift(site) : LiftSemanticInstruction(site, profile, frame);
    if(semantics)if(const auto* failure=std::get_if<SemanticFailure>(&lifted);
        failure && failure->kind==SemanticError::kMalformedInstruction)Fail(RawEventError::kCorrespondence);
    auto exceptional = ExceptionalCompletion::kImpossible;
    if (const auto* semantic = std::get_if<SemanticInstruction>(&lifted)) {
      before.registers = semantic->effects.normal_registers;
      exceptional = semantic->effects.exceptional;
    } else if (extension) {
      if (const auto effect = extension->Effect(i)) {
        before.registers = effect->registers;
        exceptional = effect->exceptional;
        before.unknown = effect->unknown;
        if (effect->resume) before.phase = RawEventPhase::kResume;
      } else before.unknown = true;
    } else before.unknown = true;
    // Inputs belong to Before; the destination exists only on success.
    commit.registers.write = before.registers.write; before.registers.write.reset();
    if (exceptional != ExceptionalCompletion::kAlwaysThrow) Add(before, static_cast<std::uint32_t>(i * kPhases + 1));
    if (exceptional != ExceptionalCompletion::kImpossible) {
      arena_.Step(1 + std::bit_width(dispatch.spans.size())); const auto target = dispatch.Find(site.offset);
      Add(before, target ? static_cast<std::uint32_t>(Site(target->handler_pc) * kPhases + 2) : Exit(i));
    }
    const auto transfer = abc::GetControlTransfer(site.decoded.opcode);
    if (!transfer) { before.unknown = true; continue; }
    const auto next = [&] {
      if (i + 1 >= count) Fail(RawEventError::kTarget, site.offset);
      Add(commit, static_cast<std::uint32_t>((i + 1) * kPhases));
    };
    switch (*transfer) {
      case abc::ControlTransfer::kFallthrough: next(); break;
      case abc::ControlTransfer::kJump:
      case abc::ControlTransfer::kBranch:
        if (!site.branch_target) Fail(RawEventError::kTarget, site.offset);
        Add(commit, static_cast<std::uint32_t>(Site(*site.branch_target) * kPhases));
        if (*transfer == abc::ControlTransfer::kBranch) next();
        break;
      case abc::ControlTransfer::kReturn: Add(commit, Exit(i)); break;
      case abc::ControlTransfer::kThrow: break;
      case abc::ControlTransfer::kSuspend:
        if (!extension || !extension->Resume(i)) before.unknown = true;
        else { next(); Add(commit, Exit(i)); }
        break;
    }
  }
  if (semantics && !semantics->Complete()) Fail(RawEventError::kCorrespondence);
  state_ = State::kReady;
}
}  // namespace abcd::analysis
