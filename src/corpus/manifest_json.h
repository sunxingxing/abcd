#ifndef ABCD_CORPUS_MANIFEST_JSON_H_
#define ABCD_CORPUS_MANIFEST_JSON_H_

#include <string_view>

#include "abcd/corpus/manifest.h"

namespace abcd::corpus {
ManifestResult DecodeManifest(std::string_view bytes);
std::variant<std::string, ManifestError> EncodeManifest(
    const std::vector<CorpusEntry>& entries);
}  // namespace abcd::corpus
#endif  // ABCD_CORPUS_MANIFEST_JSON_H_
