#include "corpus_inspection.h"

int main(int argc, char** argv) {
  return abcd::tools::RunCorpusInspection(
      argc, argv, abcd::tools::InspectionStage::kNormalFlow);
}
