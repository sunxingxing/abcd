// Capability report over abc_structure --value-source exports (S2).
//
// usage: abc_capability EXPORT_DIR... | abc_capability --list FILE
//
// Streams one manifest (and its per-record metadata) at a time and aggregates
// typed outcomes for Pareto ordering of capability gaps. It reads exports only:
// never the input ABCs, never paired source. Denominators are explicit:
// directories without a manifest are export failures, records without a
// module are counted separately, and profile refusals are kept apart from
// recovery gaps. Output is one JSON document on stdout.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "abcd/source/record_program.h"
#include "process_limit.h"

namespace {
using nlohmann::json;
// Manifest SourceStatus wire values (tools/abc_structure.cc).
constexpr unsigned kWritten = 0;
constexpr unsigned kModuleContext = 1;
constexpr unsigned kNoModule = 2;
constexpr unsigned kProgram = 3;
constexpr unsigned kIncomplete = 4;
constexpr unsigned kManifestSchema = 2;
constexpr std::size_t kTopCauses = 40;

struct Tally {
  std::uint64_t records = 0, non_module = 0, complete = 0, incomplete = 0;
  std::uint64_t module_context = 0, profile_refused = 0, failed = 0;
  json Json() const {
    return {{"records", records}, {"non_module", non_module},
            {"module_records", records - non_module}, {"complete", complete},
            {"incomplete", incomplete}, {"failed", failed},
            {"profile_refused", profile_refused}, {"module_context_failed", module_context}};
  }
};

// Leaf key: the last typed cause when present, else the coarsest typed label.
std::string LeafKey(const json& record) {
  const auto leaf = [](const json& causes) -> std::string {
    if (!causes.is_array() || causes.empty()) return {};
    const auto& last = causes.back();
    return last.value("domain", std::string{}) + "=" + std::to_string(last.value("value", 0u));
  };
  if (record.contains("source_failure")) {
    const auto& failure = record["source_failure"];
    auto key = leaf(failure.value("causes", json::array()));
    if (!key.empty()) return key;
    return "route_refusal:" + failure.value("class", std::string{}) + "/" +
           failure.value("reason", std::string{});
  }
  auto key = leaf(record.value("source_causes", json::array()));
  if (!key.empty()) return key;
  return "record_program=" + std::to_string(record.value("source_error", 0u));
}

bool HasModule(const std::filesystem::path& root, const json& record) {
  std::ifstream stream(root / record.value("metadata", std::string{}), std::ios::binary);
  if (!stream) return true;  // Unknown: keep it in the module denominator.
  const auto metadata = json::parse(stream, nullptr, false);
  return metadata.is_discarded() || !metadata.contains("module") || !metadata["module"].is_null();
}

int Run(const std::vector<std::filesystem::path>& exports,
        const abcd::tools::ProcessLimit& limit) {
  Tally total;
  std::map<std::string, Tally> by_version;
  std::map<std::string, std::uint64_t> leaves;
  std::uint64_t export_failures = 0, schema_mismatch = 0;
  for (const auto& root : exports) {
    std::ifstream stream(root / "manifest.json", std::ios::binary);
    if (!stream) { ++export_failures; continue; }
    const auto manifest = json::parse(stream, nullptr, false);
    if (manifest.is_discarded() || manifest.value("schema", 0u) != kManifestSchema) {
      ++schema_mismatch;
      continue;
    }
    std::string version = "unknown";
    if (manifest.contains("abc_version")) {
      version.clear();
      for (const auto& byte : manifest["abc_version"])
        version += (version.empty() ? "" : ".") + std::to_string(byte.get<unsigned>());
    }
    auto& per_version = by_version[version];
    for (const auto& record : manifest.value("records", json::array())) {
      for (auto* tally : {&total, &per_version}) ++tally->records;
      const auto status = record.value("source_status", kNoModule);
      const auto count = [&](std::uint64_t Tally::*field) {
        ++(total.*field);
        ++(per_version.*field);
      };
      switch (status) {
        case kNoModule: count(&Tally::non_module); break;
        case kWritten: count(&Tally::complete); break;
        case kIncomplete: count(&Tally::incomplete); break;
        case kModuleContext: count(&Tally::module_context); break;
        case kProgram:
          if (record.value("source_failure_domain", std::string{}) == "record_program" &&
              record.value("source_error", 0u) ==
                  static_cast<unsigned>(abcd::source::RecordProgramError::kProfile)) {
            // Profile refusal precedes module classification: take the module
            // denominator from the record's own metadata.
            if (HasModule(root, record)) count(&Tally::profile_refused);
            else count(&Tally::non_module);
          } else {
            count(&Tally::failed);
            ++leaves[LeafKey(record)];
          }
          break;
        default: count(&Tally::failed); ++leaves["unknown_status"]; break;
      }
    }
  }
  std::vector<std::pair<std::string, std::uint64_t>> ordered(leaves.begin(), leaves.end());
  std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  json pareto = json::array();
  for (std::size_t i = 0; i < ordered.size() && i < kTopCauses; ++i)
    pareto.push_back({{"leaf", ordered[i].first}, {"records", ordered[i].second}});
  json versions = json::object();
  for (const auto& [version, tally] : by_version) versions[version] = tally.Json();
  json report{{"exports", exports.size()}, {"export_failures", export_failures},
              {"schema_mismatch", schema_mismatch}, {"total", total.Json()},
              {"by_version", versions}, {"distinct_leaves", leaves.size()},
              {"pareto", pareto}};
  abcd::tools::ReportProcessMemory(report, limit);
  std::cout << report.dump(2) << '\n';
  return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
}
}  // namespace

int main(int argc, char** argv) {
  const auto limit = abcd::tools::EstablishProcessLimit();
  if (!abcd::tools::ProcessLimitAcceptable(limit)) {
    abcd::tools::ReportLimitFailure(std::cerr, limit);
    return EXIT_FAILURE;
  }
  std::vector<std::filesystem::path> exports;
  if (argc == 3 && std::string_view(argv[1]) == "--list") {
    std::ifstream list(argv[2]);
    for (std::string line; std::getline(list, line);)
      if (!line.empty()) exports.emplace_back(line);
    if (!list.eof()) return EXIT_FAILURE;
  } else {
    for (int i = 1; i < argc; ++i) exports.emplace_back(argv[i]);
  }
  if (exports.empty()) {
    std::cerr << "usage: abc_capability EXPORT_DIR... | abc_capability --list FILE\n";
    return EXIT_FAILURE;
  }
  try {
    return Run(exports, limit);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
