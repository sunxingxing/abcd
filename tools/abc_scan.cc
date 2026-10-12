#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <nlohmann/json.hpp>
#include <vector>

#include "abcd/corpus/scan.h"
#include "process_limit.h"

namespace {
constexpr int kRequiredArguments = 4;
constexpr int kRootArgumentWidth = 2;

int Run(int argc, char** argv) {
  if (argc < kRequiredArguments || (argc - 2) % kRootArgumentWidth != 0) {
    std::cerr
        << "usage: abc_scan MANIFEST CORPUS_ID ROOT [CORPUS_ID ROOT ...]\n";
    return EXIT_FAILURE;
  }
  const auto limit = abcd::tools::EstablishProcessLimit();
  if (!abcd::tools::ProcessLimitAcceptable(limit)) {
    abcd::tools::ReportLimitFailure(std::cerr, limit);
    return EXIT_FAILURE;
  }
  std::vector<abcd::corpus::CorpusRoot> roots;
  for (int i = 2; i < argc; i += kRootArgumentWidth)
    roots.push_back({argv[i], argv[i + 1]});
  const auto result = abcd::corpus::ScanCorpus(argv[1], roots);
  if (const auto* failure = std::get_if<abcd::corpus::ScanFailure>(&result)) {
    nlohmann::json diagnostic{{"issue", static_cast<int>(failure->issue)},
                              {"path", failure->path.generic_string()}};
    if (failure->file_error)
      diagnostic["file_error"] = static_cast<int>(*failure->file_error);
    if (failure->manifest_error) {
      diagnostic["manifest_error"] = static_cast<int>(*failure->manifest_error);
    }
    std::cerr << diagnostic.dump() << '\n';
    return EXIT_FAILURE;
  }
  const auto& stats = std::get<abcd::corpus::ScanStats>(result);
  nlohmann::json report{{"files", stats.files},
                        {"unique_contents", stats.unique_contents},
                        {"bytes_hashed", stats.bytes_hashed},
                        {"classified", stats.classified},
                        {"reused", stats.reused},
                        {"dynamic", stats.dynamic},
                        {"static", stats.static_abc},
                        {"unknown", stats.unknown},
                        {"invalid", stats.invalid},
                        {"symlinks_skipped", stats.symlinks_skipped}};
  abcd::tools::ReportProcessMemory(report, limit);
  std::cout << report.dump(2) << '\n';
  return EXIT_SUCCESS;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    return Run(argc, argv);
  } catch (const std::bad_alloc&) {
    std::cerr << "process memory budget exhausted\n";
  } catch (const std::exception& error) {
    std::cerr << "scan failed: " << error.what() << '\n';
  }
  return EXIT_FAILURE;
}
