#ifndef ABCD_TOOLS_SOURCE_CLI_OPTIONS_H_
#define ABCD_TOOLS_SOURCE_CLI_OPTIONS_H_

#include <exception>
#include <optional>
#include <ostream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace abcd::tools {
enum class SourceOptionError { kRemoved };
enum class RemovedSourceOption { kArithmetic, kNumeric, kConstructors };

// CLI boundary only: retired selectors must never become input/export paths.
// Source recovery has one default contract; raw analysis policies are unchanged.
inline std::optional<RemovedSourceOption> FindRemovedSourceOption(
    int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--language-arithmetic") return RemovedSourceOption::kArithmetic;
    if (option == "--language-numeric") return RemovedSourceOption::kNumeric;
    if (option == "--language-constructors") return RemovedSourceOption::kConstructors;
  }
  return {};
}

inline std::string_view RemovedSourceOptionName(RemovedSourceOption option) {
  switch (option) {
    case RemovedSourceOption::kArithmetic: return "--language-arithmetic";
    case RemovedSourceOption::kNumeric: return "--language-numeric";
    case RemovedSourceOption::kConstructors: return "--language-constructors";
  }
  std::terminate();
}

inline nlohmann::json RemovedSourceOptionFailure(RemovedSourceOption option) {
  return {{"domain", "abcd::tools::SourceOptionError"},
          {"value", static_cast<unsigned>(SourceOptionError::kRemoved)},
          {"option", RemovedSourceOptionName(option)},
          {"message", "removed option; language-source is now the default"}};
}

inline void ReportRemovedSourceOption(std::ostream& stream,
                                      RemovedSourceOption option) {
  stream << RemovedSourceOptionFailure(option) << '\n';
}
}  // namespace abcd::tools
#endif
