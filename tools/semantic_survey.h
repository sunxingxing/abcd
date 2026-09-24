#ifndef ABCD_TOOLS_SEMANTIC_SURVEY_H_
#define ABCD_TOOLS_SEMANTIC_SURVEY_H_

#include <iosfwd>

#include "abcd/abc/metadata_index.h"
#include "abcd/corpus/abc_file.h"

// Diagnostic only: AST admission does not establish class/module ownership.
int SurveyRecords(const abcd::corpus::AbcFile& file,
                  const abcd::abc::Header& header,
                  const abcd::abc::MetadataIndex& index,
                  const abcd::corpus::FileFingerprint& fingerprint,
                  std::ostream& output);

#endif  // ABCD_TOOLS_SEMANTIC_SURVEY_H_
