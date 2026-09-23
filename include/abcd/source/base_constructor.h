#ifndef ABCD_SOURCE_BASE_CONSTRUCTOR_H_
#define ABCD_SOURCE_BASE_CONSTRUCTOR_H_

#include "abcd/analysis/class_creation.h"
#include "abcd/source/base_function.h"

namespace abcd::source {
struct BaseConstructor {
  analysis::ClassCreation creation;
  abc::MethodAttributes attributes;
  abc::FrameLayout frame;
  std::string_view class_binding;
};
// Emits a constructor METHOD, not a whole class or record. Caller must prove
// the matching ordinary class site, constructor target/ABI, no-extends runtime
// heritage (CLASS_CONSTRUCTOR) and a stable private enclosing class binding.
// Initial method kind alone does not prove base heritage. No derived
// constructor is supported. Simple formal count must match runtime length;
// outer captures are rejected. Native class syntax preserves new-only calls,
// prototype descriptors and constructor return rules. Assignments remain body
// statements, not fields. Same preflight, budgets and atomic-publication
// obligations as WriteBaseFunction.
// A supplied method lookup enables typed nested ordinary closures only.
[[nodiscard]] FunctionEmitResult WriteBaseConstructor(
    const BaseConstructor& constructor, const abc::MethodBody& body,
    std::ostream& output, LocalLexicalBudget budget = {},
    const MethodConstants* constants = nullptr,
    const abc::MethodLookup* methods = nullptr);
}  // namespace abcd::source
#endif
