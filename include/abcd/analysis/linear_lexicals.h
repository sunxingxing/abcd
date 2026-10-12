#ifndef ABCD_ANALYSIS_LINEAR_LEXICALS_H_
#define ABCD_ANALYSIS_LINEAR_LEXICALS_H_

#include <cstdint>

#include "abcd/analysis/arithmetic_semantics.h"
#include "abcd/analysis/creation_binding.h"
#include "abcd/analysis/environment_flow.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/phi_placement.h"

namespace abcd::analysis {
// Local lexical cell identity shared by value-tree capture and binding stages.
struct LocalCellId {
  std::uint32_t value = 0;
};
struct LocalCell {
  EnvironmentId environment;
  LexicalSlot slot;
};
// Parametric child analysis only. Its caller must discharge every captured
// binding's Language premise before publishing any executable source.
enum class CapturedValues { kReject, kLanguage };
}  // namespace abcd::analysis

#endif  // ABCD_ANALYSIS_LINEAR_LEXICALS_H_
