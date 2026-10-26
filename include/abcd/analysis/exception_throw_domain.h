#ifndef ABCD_ANALYSIS_EXCEPTION_THROW_DOMAIN_H_
#define ABCD_ANALYSIS_EXCEPTION_THROW_DOMAIN_H_

#include <memory>
#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/raw_event_graph.h"
#include "abcd/analysis/exception_throw_failure.h"

namespace abcd::analysis {
// A constraint on ONE explicit throw's dispatch payload, not a global upgrade
// of its SSA value. Copies retain identity; phis remain distinct definitions.
struct ExceptionThrowDomain {
  ExceptionNodeId site;
  SsaValueId payload;
  SsaValueId compared;
  ExceptionNodeId comparison;
  ExceptionNodeId branch;
  std::uint8_t successor = 0;
  std::optional<ExceptionNodeId> definition;
  auto operator<=>(const ExceptionThrowDomain&) const = default;
};
// Equality's unequal edge excludes Hole from this same dynamic value. The
// edge must dominate the throw in the full exception CFG; a cyclic definition
// is not refined by a potentially stale comparison from a previous activation.
// O((N+E+V+U) log(N+V)), PMR scratch charged by arena before allocation.
[[nodiscard]] std::pmr::vector<ExceptionThrowDomain> DeriveExceptionThrowDomains(
    const ExceptionFlow&, const ExceptionSsa&, RawEventArena&);
void VerifyExceptionThrowDomains(const ExceptionFlow&, const ExceptionSsa&,
    std::span<const ExceptionThrowDomain>, RawEventArena&);

// Sparse SSA evidence is kept separately from the value lattice. A guard
// filters one throw-to-Catch edge only; it never refines its payload globally.
struct ThrowValueDefinition {
  SsaValueId value;
  std::optional<ExceptionNodeId> site;
  std::optional<SsaValueId> copy;
  bool phi = false;
  auto operator<=>(const ThrowValueDefinition&) const = default;
};
struct ThrowPhiInput {
  SsaValueId phi, value;
  std::optional<ExceptionNodeId> source;
  std::uint8_t successor = 0;
  auto operator<=>(const ThrowPhiInput&) const = default;
};
struct ThrowCatchSource {
  ExceptionNodeId source, destination;
  std::uint8_t successor = 0;
  std::optional<SsaValueId> payload;
  auto operator<=>(const ThrowCatchSource&) const = default;
};
enum class ThrowFactKind : std::uint8_t { kUnknown, kGuard, kFixed, kPhi, kCatch, kTransport };
struct ThrowFactUse {
  ExceptionNodeId node;
  std::optional<std::uint8_t> successor;
  auto operator<=>(const ThrowFactUse&) const = default;
};
struct ThrowFact {
  SsaValueId value;
  ThrowFactUse use;
  ThrowFactKind kind = ThrowFactKind::kUnknown;
  std::optional<ExceptionThrowDomain> guard;
  bool non_hole = false;
  auto operator<=>(const ThrowFact&) const = default;
};
struct ThrowFactInput {
  std::uint32_t fact = 0, input = 0;
  // An input is a physical phi edge or an incoming Catch source, never a
  // layout adjacency. The index names the corresponding coverage table.
  std::optional<std::uint32_t> correspondence;
  auto operator<=>(const ThrowFactInput&) const = default;
};
struct ThrowConstraint {
  ExceptionNodeId site;
  SsaValueId payload;
  std::uint32_t fact = 0;
  auto operator<=>(const ThrowConstraint&) const = default;
};
struct GuardedCatchPayloads {
  explicit GuardedCatchPayloads(std::pmr::memory_resource* memory)
      : throws(memory), definitions(memory), phis(memory), reads(memory), catches(memory), facts(memory), inputs(memory), constraints(memory) {}
  std::pmr::vector<ExceptionThrowDomain> throws;
  std::pmr::vector<ThrowValueDefinition> definitions;
  std::pmr::vector<ThrowPhiInput> phis;
  std::pmr::vector<SsaRead> reads;
  std::pmr::vector<ThrowCatchSource> catches;
  std::pmr::vector<ThrowFact> facts;
  std::pmr::vector<ThrowFactInput> inputs;
  std::pmr::vector<ThrowConstraint> constraints;
};
// Validate SSA definitions/uses and every phi predecessor against the full
// exception CFG. Definition dominance plus the selected edge's dominance at
// the use proves a CURRENT SSA activation even within an SCC. Copies cannot
// cross a phi/Catch definition. O((N+E+V+U) log(N+V)); all storage is prepaid.
GuardedCatchPayloads DeriveGuardedCatchPayloads(
    const ExceptionFlow&, const ExceptionSsa&, RawEventArena&);
void VerifyGuardedCatchPayloads(const ExceptionFlow&, const ExceptionSsa&,
    const GuardedCatchPayloads&, RawEventArena&);

// Shared immutable owner survives imported/edited graphs and recovery trees.
// No pointers into a moving PhasedBody or a query-local arena are retained.
struct GuardedCatchEvidence {
  GuardedCatchEvidence(std::shared_ptr<const ExceptionFlow> flow,
      std::shared_ptr<const ExceptionSsa> ssa, detail::IdfScratch& memory, std::uint64_t work)
      : flow(std::move(flow)), ssa(std::move(ssa)), arena({memory.Available(),work}), proof(arena.Resource()) {}
  std::shared_ptr<const ExceptionFlow> flow;
  std::shared_ptr<const ExceptionSsa> ssa;
  std::size_t owner_bytes = 0;
  RawEventArena arena;
  GuardedCatchPayloads proof;
};
std::uint64_t VerifyGuardedCatchEvidence(
    const GuardedCatchEvidence&, SemanticBudget);
}  // namespace abcd::analysis
#endif
