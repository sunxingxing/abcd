#include "protocol_rebase.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/analysis/exception_dispatch.h"

namespace abcd::source {
namespace {
using Error = ProtocolRebaseError;
void Require(bool ok, Error error = Error::kInput) { if (!ok) throw error; }
class Rebaser {
 public:
  Rebaser(const ProtocolRebaseInput& input, analysis::SemanticBudget budget)
      : in_(input), initial_(budget), left_(budget) {}
  void Step(std::uint64_t n = 1) {
    Require(n <= left_.work, Error::kWorkLimit); left_.work -= n;
  }
  void Admit(std::size_t n, std::size_t width = 1) {
    Require(n <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= n * width;
  }
  template<class T> void Array(std::vector<T>& out, std::size_t n) {
    Admit(n, sizeof(T)); out.reserve(n); Admit(out.capacity() - n, sizeof(T));
    Step(n); out.resize(n);
  }
  void Prepare() {
    const auto& old = in_.before.instructions;
    const auto& now = in_.after.instructions;
    Require(!old.empty() && !now.empty() &&
        (in_.before_origins.empty() || in_.before_origins.size() == old.size()) &&
        in_.after_origins.size() == now.size());
    Array(mapping_, old.size());
    std::size_t next = 0;
    for (std::size_t i = 0; i < old.size(); ++i) {
      Step();
      Require(old[i].decoded.size && (!i || old[i - 1].offset < old[i].offset));
      const auto origin = Origin(i);
      Require(!i || Origin(i - 1) < origin);
      if (next == now.size() || origin < in_.after_origins[next]) continue;
      Require(origin == in_.after_origins[next]);
      Require(now[next].decoded.size == old[i].decoded.size &&
          now[next].decoded.opcode == old[i].decoded.opcode &&
          now[next].decoded.operand_count == old[i].decoded.operand_count &&
          now[next].decoded.jump == old[i].decoded.jump &&
          now[next].decoded.conditional == old[i].decoded.conditional &&
          now[next].decoded.accumulator_read == old[i].decoded.accumulator_read &&
          now[next].decoded.accumulator_write == old[i].decoded.accumulator_write &&
          (!next || in_.after_origins[next - 1] < in_.after_origins[next]) &&
          (!next || now[next - 1].offset.value + now[next - 1].decoded.size == now[next].offset.value));
      Require(old[i].decoded.operand_count <= old[i].decoded.operands.size());
      for (std::size_t j = 0; j < old[i].decoded.operand_count; ++j) {
        Step(); const auto& a = old[i].decoded.operands[j]; const auto& b = now[next].decoded.operands[j];
        Require(a.raw_bits == b.raw_bits && a.kind == b.kind && a.access == b.access &&
            a.width_bits == b.width_bits && a.is_signed == b.is_signed && a.is_float == b.is_float);
      }
      mapping_[i] = now[next++].offset;
    }
    Require(next == now.size());
    for (std::size_t i = 0; i < in_.folds.size(); ++i) {
      Step(); const auto& f = in_.folds[i];
      Require(f.begin < f.end && (!i || in_.folds[i - 1].end <= f.begin));
      Index(f.begin); Index(f.end);
    }
    for (std::size_t i = 0; i < old.size(); ++i) {
      Step();
      Require(mapping_[i] || Folded(old[i].offset));
      if (!mapping_[i] || Folded(old[i].offset)) continue;
      Step(1 + std::bit_width(now.size()));
      const auto at = std::lower_bound(now.begin(), now.end(), *mapping_[i],
          [](const auto& s, auto pc) { return s.offset < pc; });
      Require(old[i].branch_target.has_value() == at->branch_target.has_value());
      if (old[i].branch_target) {
        const auto target = Index(*old[i].branch_target);
        Require(mapping_[target] && mapping_[target] == at->branch_target);
        Require(at->decoded.branch_delta ==
            std::int64_t(at->branch_target->value) - at->offset.value);
      } else Require(old[i].decoded.branch_delta == at->decoded.branch_delta);
    }
    // No selected-handler comparison is needed on an identity transaction.
    // Nontrivial folds retain the original catch even after TryId renumbering.
    if (!in_.folds.empty()) Dispatch();
    Array(claimed_, now.size());
  }
  abc::CodeOffset Origin(std::size_t i) const {
    return in_.before_origins.empty() ? in_.before.instructions[i].offset : in_.before_origins[i];
  }
  std::size_t Index(abc::CodeOffset pc) {
    Step(1 + std::bit_width(in_.before.instructions.size()));
    const auto& sites = in_.before.instructions;
    const auto at = std::lower_bound(sites.begin(), sites.end(), pc,
        [](const auto& site, auto p) { return site.offset < p; });
    Require(at != sites.end() && at->offset == pc, Error::kAnchor);
    return static_cast<std::size_t>(at - sites.begin());
  }
  bool Folded(abc::CodeOffset pc) {
    Step(1 + std::bit_width(in_.folds.size()));
    const auto at = std::upper_bound(in_.folds.begin(), in_.folds.end(), pc,
        [](auto p, const auto& f) { return p < f.begin; });
    return at != in_.folds.begin() && pc < (at - 1)->end;
  }
  abc::CodeOffset Map(abc::CodeOffset pc, bool claim = false) {
    const auto i = Index(pc);
    Require(mapping_[i] && !Folded(pc), Error::kAnchor);
    const auto mapped = *mapping_[i];
    if (claim) {
      Step(1 + std::bit_width(in_.after.instructions.size()));
      const auto& sites = in_.after.instructions;
      const auto at = std::lower_bound(sites.begin(), sites.end(), mapped,
          [](const auto& site, auto p) { return site.offset < p; });
      Require(at != sites.end() && at->offset == mapped, Error::kAnchor);
      auto& used = claimed_[at - sites.begin()];
      Require(!used, Error::kCollision); used = 1;
    }
    return mapped;
  }
  void Interval(abc::CodeOffset first, abc::CodeOffset last) {
    Require(first < last, Error::kAnchor);
    Step(1 + std::bit_width(in_.folds.size()));
    const auto at = std::lower_bound(in_.folds.begin(), in_.folds.end(), first,
        [](const auto& f, auto p) { return f.end <= p; });
    Require(at == in_.folds.end() || last < at->begin, Error::kAnchor);
  }
  // Most raw resume instructions were already removed by the owner. If an
  // original anchor survives into THIS fold, it must survive this fold too.
  void Original(abc::CodeOffset pc) {
    Step(1 + std::bit_width(in_.before.instructions.size()));
    if (in_.before_origins.empty()) {
      const auto& sites = in_.before.instructions;
      const auto at = std::lower_bound(sites.begin(), sites.end(), pc,
          [](const auto& site, auto p) { return site.offset < p; });
      if (at != sites.end() && at->offset == pc) Map(pc);
    } else {
      const auto at = std::lower_bound(in_.before_origins.begin(), in_.before_origins.end(), pc);
      if (at != in_.before_origins.end() && *at == pc)
        Map(in_.before.instructions[at - in_.before_origins.begin()].offset);
    }
  }
  analysis::AwaitSite Await(const analysis::AwaitSite& old) {
    Require(Origin(Index(old.pc)) == old.origin.await &&
        Origin(Index(old.mode_pc)) == old.origin.mode, Error::kAnchor);
    Interval(old.pc, old.mode_pc);
    auto result = old; result.pc = Map(old.pc, true); result.mode_pc = Map(old.mode_pc, true);
    const auto& o = old.origin;
    for (auto p : {o.await, o.suspend, o.resume, o.result_store, o.mode, o.mode_store,
                   o.branch, o.reject, o.continuation}) Original(p);
    if (o.instructions) for (auto p : *o.instructions) Original(p);
    return result;
  }
  analysis::YieldSite Yield(const analysis::YieldSite& old) {
    Require(Origin(Index(old.pc)) == old.origin.create &&
        (!old.mode_pc || Origin(Index(*old.mode_pc)) == old.origin.mode), Error::kAnchor);
    auto result = old; result.pc = Map(old.pc, true);
    if (old.mode_pc) { Interval(old.pc, *old.mode_pc); result.mode_pc = Map(*old.mode_pc, true); }
    const auto& o = old.origin;
    for (auto p : {o.create, o.suspend, o.resume, o.result_store, o.mode, o.mode_store,
                   o.continuation, o.branch, o.reject}) Original(p);
    if (o.return_route) {
      const auto& r = *o.return_route;
      for (auto p : {r.branch, r.cleanup, r.payload}) Original(p);
      if (r.completion) Original(*r.completion);
    }
    if (o.awaited_rejections) {
      Original(o.awaited_rejections->value); Original(o.awaited_rejections->returned);
    }
    return result;
  }
  std::uint64_t Work() const { return initial_.work - left_.work; }
  std::size_t Bytes() const { return initial_.bytes - left_.bytes; }
 private:
  analysis::ExceptionDispatch DispatchFor(const abc::MethodBody& body) {
    const auto n = body.instructions.size() + body.tries.size() + body.handlers.size();
    // Shared dispatch owns its scratch quota. Prepay its sorting/sweep work.
    Step(16 * n * (1 + std::bit_width(n)));
    auto built = analysis::BuildExceptionDispatch(body, in_.profile, {left_.bytes});
    if (const auto* f = std::get_if<analysis::DispatchFailure>(&built))
      throw f->kind == analysis::DispatchError::kResourceLimit ? Error::kResourceLimit : Error::kDispatch;
    auto result = std::get<analysis::ExceptionDispatch>(std::move(built));
    Admit(result.spans.capacity(), sizeof(analysis::DispatchSpan)); return result;
  }
  void Dispatch() {
    auto old = DispatchFor(in_.before), now = DispatchFor(in_.after);
    for (std::size_t i = 0; i < mapping_.size(); ++i) {
      Step(); const auto pc = in_.before.instructions[i].offset;
      if (!mapping_[i] || Folded(pc)) continue;
      Step(2 * (1 + std::bit_width(old.spans.size() + now.spans.size())));
      const auto a = old.Find(pc), b = now.Find(*mapping_[i]);
      Require(a.has_value() == b.has_value(), Error::kDispatch);
      if (!a) continue;
      Require(a->handler.value < in_.before.handlers.size() && b->handler.value < in_.after.handlers.size(), Error::kDispatch);
      const auto target = Index(a->handler_pc);
      Require(mapping_[target] && *mapping_[target] == b->handler_pc &&
          in_.before.handlers[a->handler.value].type == in_.after.handlers[b->handler.value].type, Error::kDispatch);
    }
  }
  const ProtocolRebaseInput& in_;
  analysis::SemanticBudget initial_, left_;
  std::vector<std::optional<abc::CodeOffset>> mapping_;
  std::vector<std::uint8_t> claimed_;
};
template<class T, class F> std::variant<T, Error> Checked(F&& f) {
  try { return f(); }
  catch (Error e) { return e; }
  catch (const std::bad_alloc&) { return Error::kResourceLimit; }
  catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<RebasedProtocols, ProtocolRebaseError> RebaseProtocols(
    const ProtocolRebaseInput& input, analysis::SemanticBudget budget) {
  return Checked<RebasedProtocols>([&] {
    if (input.awaits.empty() && input.yields.empty()) return RebasedProtocols{};
    Rebaser q(input, budget); q.Prepare(); RebasedProtocols result;
    q.Array(result.awaits, input.awaits.size()); q.Array(result.yields, input.yields.size());
    for (std::size_t i = 0; i < input.awaits.size(); ++i) result.awaits[i] = q.Await(input.awaits[i]);
    for (std::size_t i = 0; i < input.yields.size(); ++i) result.yields[i] = q.Yield(input.yields[i]);
    result.allocated_bytes = q.Bytes(); result.work = q.Work(); return result;
  });
}
std::variant<std::uint64_t, ProtocolRebaseError> VerifyRebasedProtocols(
    const ProtocolRebaseInput& input, const RebasedProtocols& actual,
    analysis::SemanticBudget budget) {
  return Checked<std::uint64_t>([&] {
    if (input.awaits.empty() && input.yields.empty()) {
      Require(actual.awaits.empty() && actual.yields.empty()); return std::uint64_t{0};
    }
    Rebaser q(input, budget); q.Prepare();
    Require(actual.awaits.size() == input.awaits.size() && actual.yields.size() == input.yields.size());
    for (std::size_t i = 0; i < input.awaits.size(); ++i) {
      const auto expected = q.Await(input.awaits[i]); const auto& a = actual.awaits[i];
      Require(expected.pc == a.pc && expected.mode_pc == a.mode_pc && expected.origin == a.origin, Error::kAnchor);
    }
    for (std::size_t i = 0; i < input.yields.size(); ++i) {
      const auto expected = q.Yield(input.yields[i]); const auto& a = actual.yields[i];
      Require(expected.pc == a.pc && expected.mode_pc == a.mode_pc && expected.origin == a.origin, Error::kAnchor);
    }
    return q.Work();
  });
}
}  // namespace abcd::source
