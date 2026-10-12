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
#include <string_view>
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
constexpr std::string_view kValueSourceView = "declarations_with_value_source";
constexpr std::size_t kTopCauses = 40;

struct Tally {
  std::uint64_t records = 0, non_module = 0, module_unknown = 0, complete = 0, incomplete = 0;
  std::uint64_t module_context = 0, profile_refused = 0, failed = 0;
  // Subsets of module_unknown: records without structural metadata.
  std::uint64_t structure_failed = 0, unknown_status = 0;
  json Json() const {
    return {{"records", records}, {"non_module", non_module},
            {"module_unknown", module_unknown},
            {"module_records", records - non_module - module_unknown}, {"complete", complete},
            {"incomplete", incomplete}, {"failed", failed},
            {"profile_refused", profile_refused}, {"module_context_failed", module_context},
            {"structure_failed", structure_failed}, {"unknown_status", unknown_status}};
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

// Module classification is independent of the recovery outcome: it comes
// from the record's own structural metadata. Unreadable metadata is unknown,
// never silently folded into either denominator.
enum class ModuleClass { kModule, kNonModule, kUnknown };
ModuleClass ClassifyModule(const std::filesystem::path& root, const json& record) {
  std::ifstream stream(root / record.value("metadata", std::string{}), std::ios::binary);
  if (!stream) return ModuleClass::kUnknown;
  const auto metadata = json::parse(stream, nullptr, false);
  if (metadata.is_discarded() || !metadata.contains("module")) return ModuleClass::kUnknown;
  return metadata["module"].is_null() ? ModuleClass::kNonModule : ModuleClass::kModule;
}

int Run(const std::vector<std::filesystem::path>& exports,
        const abcd::tools::ProcessLimit& limit) {
  Tally total;
  std::map<std::string, Tally> by_version;
  std::map<std::string, std::uint64_t> leaves;
  std::uint64_t export_failures = 0, schema_mismatch = 0, view_mismatch = 0;
  for (const auto& root : exports) {
    std::ifstream stream(root / "manifest.json", std::ios::binary);
    if (!stream) { ++export_failures; continue; }
    const auto manifest = json::parse(stream, nullptr, false);
    if (manifest.is_discarded() || manifest.value("schema", 0u) != kManifestSchema) {
      ++schema_mismatch;
      continue;
    }
    // Only value-source exports carry recovery outcomes.
    if (manifest.value("view", std::string{}) != kValueSourceView) {
      ++view_mismatch;
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
      const auto count = [&](std::uint64_t Tally::*field) {
        ++(total.*field);
        ++(per_version.*field);
      };
      // A record without a source status has no recovery outcome and, for a
      // structural failure, no metadata: its module class is unknown. Never
      // default it to "no module".
      if (!record.contains("source_status")) {
        count(&Tally::module_unknown);
        if (record.contains("structure_failure")) {
          count(&Tally::structure_failed);
          const auto& typed = record["structure_failure"];
          ++leaves["structure:" + typed.value("domain", std::string{}) + "=" +
                   std::to_string(typed.value("value", 0u))];
        } else {
          count(&Tally::unknown_status);
        }
        continue;
      }
      const auto status = record["source_status"].get<unsigned>();
      if (status == kNoModule) { count(&Tally::non_module); continue; }
      if (status == kWritten) { count(&Tally::complete); continue; }
      if (status == kIncomplete) { count(&Tally::incomplete); continue; }
      // Failures may precede module classification (profile, whole-file
      // index): classify from metadata before counting the outcome.
      const auto module = ClassifyModule(root, record);
      if (module == ModuleClass::kNonModule) { count(&Tally::non_module); continue; }
      if (module == ModuleClass::kUnknown) { count(&Tally::module_unknown); continue; }
      if (status == kModuleContext) { count(&Tally::module_context); continue; }
      if (status == kProgram &&
          record.value("source_failure_domain", std::string{}) == "record_program" &&
          record.value("source_error", 0u) ==
              static_cast<unsigned>(abcd::source::RecordProgramError::kProfile)) {
        count(&Tally::profile_refused);
        continue;
      }
      count(&Tally::failed);
      ++leaves[status == kProgram ? LeafKey(record) : "unknown_status"];
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
              {"schema_mismatch", schema_mismatch}, {"view_mismatch", view_mismatch},
              {"total", total.Json()},
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
