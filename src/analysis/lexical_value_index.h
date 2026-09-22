#ifndef ABCD_ANALYSIS_LEXICAL_VALUE_INDEX_H_
#define ABCD_ANALYSIS_LEXICAL_VALUE_INDEX_H_

#include <memory_resource>

#include "abcd/analysis/environment_escape.h"
#include "abcd/analysis/register_value_flow.h"

namespace abcd::analysis {
struct RegisterValueFlow::LexicalIndex {
  struct CellId {
    std::size_t value = 0;
  };
  struct CellKey {
    EnvironmentId environment;
    LexicalSlot slot;
  };
  enum class AccessKind { kLoad, kStore };
  struct Access {
    CellId cell;
    SsaValueId value;
    AccessKind kind;
  };
  LexicalIndex(const abc::MethodBody& input, const RegisterSsa& values,
               std::uint64_t frame, IdfQuota& work,
               std::pmr::memory_resource* memory)
      : body(input),
        ssa(values),
        frame_size(frame),
        quota(work),
        cells(memory),
        accesses(memory) {}
  void Collect(const LexicalValueContext& context);
  void Charge(std::uint64_t units = 1);
  static bool Less(const CellKey& a, const CellKey& b);
  const abc::MethodBody& body;
  const RegisterSsa& ssa;
  std::uint64_t frame_size;
  IdfQuota& quota;
  std::pmr::vector<CellKey> cells;
  std::pmr::vector<Access> accesses;
};
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_LEXICAL_VALUE_INDEX_H_
