#ifndef ABCD_ANALYSIS_NORMALIZED_RAW_SEMANTICS_H_
#define ABCD_ANALYSIS_NORMALIZED_RAW_SEMANTICS_H_
#include "abcd/analysis/raw_event_graph.h"
#include "semantic_lifter.h"
namespace abcd::analysis::detail {
// Replay exactly the semantic owner used by ExceptionFlow. This adapter does
// not prove the async protocol; PhasedBody's fresh async owner does that before
// normalization. No allocation, extra query order or resume edge is introduced.
class NormalizedRawSemantics final : public RawEventSemantics {
 public:
  NormalizedRawSemantics(abc::ProfileId profile,std::uint64_t frame,
      std::span<const AwaitSite> awaits,const abc::FrameLayout* layout=nullptr)
      :lifter_(profile,frame,awaits,layout) {}
  InstructionLift Lift(const abc::BodyInstruction& site) override {return lifter_.Lift(site);}
  bool Complete() const override {return lifter_.Complete();}
 private:
  SemanticLifter lifter_;
};
}
#endif
