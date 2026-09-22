#ifndef ABCD_ANALYSIS_ENVIRONMENT_ESCAPE_BUILDER_H_
#define ABCD_ANALYSIS_ENVIRONMENT_ESCAPE_BUILDER_H_

#include <memory_resource>

#include "abcd/analysis/environment_escape.h"
#include "idf_scratch.h"

namespace abcd::analysis {
struct EnvironmentEscape::Builder {
  struct AliasId {
    std::size_t value;
    bool operator==(const AliasId&) const = default;
  };
  struct Containment {
    AliasId owner;
    AliasId value;
  };
  Builder(const abc::MethodBody& body, const RegisterSsa& ssa,
          const EnvironmentFlow& flow, std::uint64_t frame_size,
          IdfQuota& quota, IdfBudget budget)
      : body(body),
        ssa(ssa),
        flow(flow),
        frame_size(frame_size),
        quota(quota),
        memory(budget.bytes),
        cells(&memory),
        parents(&memory),
        ranks(&memory),
        exposed(&memory),
        edges(&memory) {}
  void Charge(std::uint64_t units = 1) {
    if (!quota.Take(units)) throw detail::IdfWorkLimit{};
  }
  AliasId Find(AliasId id);
  void Unite(AliasId a, AliasId b);
  void Expose(AliasId id) { exposed[Find(id).value] = 1; }
  AliasId Value(SsaValueId id) const;
  AliasId Environment(EnvironmentRef ref) const;
  AliasId Cell(const LexicalBinding& binding);
  AliasId Write(InstructionId id) const;
  AliasId Read(InstructionId id) const;
  void Extract();
  void Propagate();
  static bool CellLess(const LexicalBinding& a, const LexicalBinding& b);

  const abc::MethodBody& body;
  const RegisterSsa& ssa;
  const EnvironmentFlow& flow;
  std::uint64_t frame_size;
  IdfQuota& quota;
  detail::IdfScratch memory;
  std::pmr::vector<LexicalBinding> cells;
  std::pmr::vector<AliasId> parents;
  std::pmr::vector<std::uint8_t> ranks, exposed;
  std::pmr::vector<Containment> edges;
  std::size_t environment_begin = 0;
  std::size_t captured = 0;
  std::size_t cell_begin = 0;
  EscapeWork work;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ENVIRONMENT_ESCAPE_BUILDER_H_
