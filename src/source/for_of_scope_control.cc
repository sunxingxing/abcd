#include <algorithm>
#include <memory_resource>

#include "../analysis/idf_scratch.h"
#include "for_of_scope.h"
namespace abcd::source {
void PlaceSinglePassScopes(const analysis::ValueProgram& g, ExceptionControl& c,
                           const SinglePassScopes& proof, ControlQuota& q) {
  using namespace analysis;
  // Bound sparse lexical incidences before growing their ordered index.
  constexpr std::size_t kScopeIncidencesPerNode = 8;
  const auto need = [](bool b) {
    if (!b) throw ControlError::kInvalidGraph;
  };
  // The ordinary graph has already been checked. This is a lexical cut,
  // retaining every original unit/arc and every physical natural-loop ID.
  const auto original = c.skeleton.nodes.size();
  std::size_t count = 0;
  for (const auto& s : proof.scopes) {
    q.Work();
    count += s.has_value();
  }
  need(count <= UINT32_MAX - original);
  q.Size(c.iterator_scopes, count);
  detail::IdfScratch memory(q.Remaining().bytes);
  std::pmr::vector<std::optional<ControlId>> raw(g.phases.size(), {}, &memory);
  for (std::uint32_t n = 0; n < c.units.size(); ++n) {
    q.Work();
    if (const auto* p = std::get_if<PhaseControlUnit>(&c.units[n])) {
      const auto r = g.phases[p->block.value].input;
      need(r.value < raw.size() && !raw[r.value]);
      raw[r.value] = ControlId{n};
    }
  }
  std::pmr::vector<std::optional<ControlId>> replacements(original, {},
                                                          &memory);
  std::pmr::vector<std::optional<std::uint32_t>> slots(proof.scopes.size(), {},
                                                       &memory);
  std::size_t at = 0;
  for (const auto& scope : proof.scopes)
    if (scope) {
      q.Work();
      const auto& s = *scope;
      need(s.entry.value < raw.size() && raw[s.entry.value] &&
           s.continuation.value < raw.size() && raw[s.continuation.value]);
      const auto entry = *raw[s.entry.value],
                 follow = *raw[s.continuation.value];
      need(!replacements[entry.value]);
      const ControlId wrapper{static_cast<std::uint32_t>(original + at)};
      c.iterator_scopes[at] = {s.protocol, wrapper, entry, follow};
      slots[s.protocol.value] = static_cast<std::uint32_t>(at++);
      replacements[entry.value] = wrapper;
    }
  using Member = std::pair<std::uint32_t, std::uint32_t>;
  std::pmr::vector<Member> members(&memory);
  members.reserve(proof.coverage.size() + c.units.size());
  for (const auto& m : proof.coverage) {
    q.Work();
    need(m.node.value < raw.size());
    if (raw[m.node.value])
      members.push_back({raw[m.node.value]->value, *slots[m.protocol.value]});
  }
  // Synthetic close/try/loop entries inherit the original source member, not
  // an interval overlap or a borrowed natural-loop header.
  std::sort(members.begin(), members.end(), [&](auto a, auto b) {
    q.Work();
    return a < b;
  });
  const auto prefix = members.size();
  for (std::uint32_t n = 0; n < original; ++n) {
    q.Work();
    std::optional<ControlId> source;
    if (n < c.units.size()) {
      if (const auto* t = std::get_if<TryControlUnit>(&c.units[n])) {
        need(t->region.value < c.regions.size() && c.regions[t->region.value]);
        source = c.regions[t->region.value]->body;
      }
      if (const auto* close = std::get_if<ProtocolCloseUnit>(&c.units[n])) {
        need(close->entry.source.value < raw.size());
        source = raw[close->entry.source.value];
      }
    } else if (const auto* l =
                   std::get_if<LoopControl>(&c.skeleton.nodes[n].synthetic)) {
      need(l->loop.value < c.skeleton.loops.size());
      source = c.skeleton.loops[l->loop.value].body;
    }
    if (!source) continue;
    auto m = static_cast<std::size_t>(
        std::lower_bound(members.begin(), members.begin() + prefix,
                         Member{source->value, 0}) -
        members.begin());
    for (; m < prefix && members[m].first == source->value; ++m) {
      q.Work();
      if (members.size() >
          kScopeIncidencesPerNode * (g.phases.size() + c.units.size() + 1))
        throw ControlError::kResourceLimit;
      const auto slot = members[m].second;
      members.push_back({n, slot});
    }
  }
  std::sort(members.begin(), members.end(), [&](auto a, auto b) {
    q.Work();
    return a < b;
  });
  // Cut the lexical continuation only inside this logical activation. The
  // body cannot fall out here: raw coverage proves every such path terminal.
  for (const auto [n, s] : members) {
    q.Work();
    auto& node = c.skeleton.nodes[n];
    const auto follow = c.iterator_scopes[s].continuation;
    const auto cut = [&](std::optional<ControlId>& p) {
      if (p == follow) p.reset();
    };
    cut(node.next);
    if (node.arms) {
      cut(node.arms->taken);
      cut(node.arms->fallthrough);
    }
    if (n < c.units.size())
      if (const auto* close = std::get_if<ProtocolCloseUnit>(&c.units[n]);
          close && close->protocol == c.iterator_scopes[s].protocol &&
          close->pending.kind == PendingCompletionKind::kBreak) {
        const auto& scope = *proof.scopes[close->protocol.value];
        if (const auto* target =
                std::get_if<ExceptionNodeId>(&close->pending.target);
            target && (*target == scope.exit || *target == scope.continuation))
          node.next.reset();
      }
  }
  const auto replace = [&](std::optional<ControlId>& id) {
    if (id && id->value < replacements.size() && replacements[id->value])
      id = replacements[id->value];
  };
  for (auto& node : c.skeleton.nodes) {
    q.Work();
    replace(node.next);
    if (node.arms) {
      replace(node.arms->taken);
      replace(node.arms->fallthrough);
    }
    if (node.decision) {
      auto id = std::optional{node.decision->evaluate};
      replace(id);
      node.decision->evaluate = *id;
      id = node.decision->join;
      replace(id);
      node.decision->join = *id;
      replace(node.decision->predicate);
    }
  }
  auto entry = std::optional{c.skeleton.entry};
  replace(entry);
  c.skeleton.entry = *entry;
  for (auto& loop : c.skeleton.loops) {
    q.Work();
    need(!replacements[loop.header.value]);
    entry = loop.body;
    replace(entry);
    loop.body = *entry;
  }
  for (auto& region : c.regions)
    if (region) {
      q.Work();
      entry = region->body;
      replace(entry);
      region->body = *entry;
      entry = region->handler;
      replace(entry);
      region->handler = *entry;
    }
  q.Admit(memory.Peak(), 1);
  q.Admit(original + count, sizeof(ControlNode));
  c.skeleton.nodes.reserve(original + count);
  q.Admit(c.skeleton.nodes.capacity() - original - count, sizeof(ControlNode));
  for (const auto& s : c.iterator_scopes) {
    q.Work();
    c.skeleton.nodes.push_back(
        {{}, {}, s.continuation, IteratorScopeControl{s.protocol}, {}});
  }
  c.allocated_bytes += c.skeleton.nodes.capacity() * sizeof(ControlNode) +
                       c.iterator_scopes.capacity() * sizeof(SinglePassControl);
}
}  // namespace abcd::source
