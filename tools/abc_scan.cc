#include <sys/resource.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

#include <nlohmann/json.hpp>

#include "abcd/corpus/scan.h"

namespace {
// Conservative process-level ceiling: virtual address space also bounds RSS.
// Library users still need a host-wide budget; this CLI launches no children.
constexpr rlim_t kProcessMemoryLimit = 1000000000;
constexpr std::uint64_t kBytesPerKibibyte = 1024;
constexpr int kRequiredArguments = 4;
constexpr int kRootArgumentWidth = 2;

bool LimitAddressSpace() {
  rlimit limit{};
  if (getrlimit(RLIMIT_AS, &limit) != 0) return false;
  limit.rlim_cur = std::min(limit.rlim_cur, kProcessMemoryLimit);
  return setrlimit(RLIMIT_AS, &limit) == 0;
}

int Run(int argc, char** argv) {
  if (argc < kRequiredArguments || (argc - 2) % kRootArgumentWidth != 0) {
    std::cerr << "usage: abc_scan MANIFEST CORPUS_ID ROOT [CORPUS_ID ROOT ...]\n";
    return EXIT_FAILURE;
  }
  if (!LimitAddressSpace()) {
    std::cerr << "cannot establish process memory limit\n";
    return EXIT_FAILURE;
  }
  std::vector<abcd::corpus::CorpusRoot> roots;
  for (int i = 2; i < argc; i += kRootArgumentWidth) roots.push_back({argv[i], argv[i+1]});
  const auto result = abcd::corpus::ScanCorpus(argv[1], roots);
  if (const auto* failure = std::get_if<abcd::corpus::ScanFailure>(&result)) {
    nlohmann::json diagnostic{{"issue", static_cast<int>(failure->issue)},
                              {"path", failure->path.generic_string()}};
    if (failure->file_error) diagnostic["file_error"] = static_cast<int>(*failure->file_error);
    if (failure->manifest_error) {
      diagnostic["manifest_error"] = static_cast<int>(*failure->manifest_error);
    }
    std::cerr << diagnostic.dump() << '\n';
    return EXIT_FAILURE;
  }
  const auto& stats = std::get<abcd::corpus::ScanStats>(result);
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    std::cerr << "cannot measure process memory\n";
    return EXIT_FAILURE;
  }
  std::cout << nlohmann::json{
      {"files", stats.files}, {"unique_contents", stats.unique_contents},
      {"bytes_hashed", stats.bytes_hashed}, {"classified", stats.classified},
      {"reused", stats.reused}, {"dynamic", stats.dynamic},
      {"static", stats.static_abc}, {"unknown", stats.unknown},
      {"invalid", stats.invalid}, {"symlinks_skipped", stats.symlinks_skipped},
      {"peak_rss_bytes", static_cast<std::uint64_t>(usage.ru_maxrss) * kBytesPerKibibyte},
      {"address_space_limit_bytes", kProcessMemoryLimit}}.dump(2) << '\n';
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
