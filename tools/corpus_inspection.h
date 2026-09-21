#ifndef ABCD_TOOLS_CORPUS_INSPECTION_H_
#define ABCD_TOOLS_CORPUS_INSPECTION_H_

namespace abcd::tools {
enum class InspectionStage {
  kMetadata,
  kInstructions,
  kNormalFlow,
  kExceptionDispatch,
  kReferences,
  kModules,
  kDominators,
  kFrames,
  kSourceBodies,
  kEnvironments,
  kCreations
};
int RunCorpusInspection(int argc, char** argv, InspectionStage stage);
}  // namespace abcd::tools
#endif  // ABCD_TOOLS_CORPUS_INSPECTION_H_
