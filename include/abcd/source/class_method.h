#ifndef ABCD_SOURCE_CLASS_METHOD_H_
#define ABCD_SOURCE_CLASS_METHOD_H_
#include "abcd/abc/class_buffer.h"
#include "abcd/source/base_function.h"

namespace abcd::source {
// FunctionTemplate/Method kind at member materialization, NOT the file flag.
enum class EffectiveMethodKind { kUnknown, kOrdinary, kGetter, kSetter };
struct ClassMethod {
  abc::ClassMethod member;
  abc::MethodAttributes attributes;
  abc::FrameLayout frame;
  abc::ClassPlacement placement;
  std::u16string_view key;
  std::string_view self_binding;
  EffectiveMethodKind effective_kind = EffectiveMethodKind::kUnknown;
};
// Native class method/accessor, not a constructible function expression.
// Caller proves class-buffer or DEFINE-method provenance, target/ABI, runtime
// name equal to key and descriptor placement. Ordinary methods additionally
// need an immutable self_binding holding their exact function object: reading
// Class.prototype[key] at invocation is NOT valid because it is mutable.
// Default output rejects outer captures. kLanguage requires a typed capture
// sink whose owner binds every obligation at the actual creation environment;
// an unbound template is never standalone executable source.
// Templates need initial NONE and a proven effective method kind. Accessors
// additionally need an unobserved function ABI slot: native get/set syntax
// has no lexical self binding to initialize that slot.
// v7.0 DefineFunctionTemplate mutates shared MethodLiteral kind;
// an earlier conflicting template may change NONE before this site's creation.
// Caller must prove the effective runtime kind, including cross-role conflicts;
// initial NONE alone is not evidence. Unknown evidence is rejected by default.
// Placement/key descriptor and reserved-property compatibility are caller
// obligations. A v7 no-await async ordinary method requires an exact
// completion-template proof; await/resume, async accessors, home-object
// operations, and generators remain unsupported.
// Simple formal count, preflight, budgets and publication match BaseFunction.
// A supplied method lookup enables typed nested ordinary closures. Unsupported
// child kinds reject the entire method instead of changing function semantics.
[[nodiscard]] FunctionEmitResult WriteClassMethod(
    const ClassMethod& method, const abc::MethodBody& body,
    std::ostream& output, LocalLexicalBudget budget = {},
    const MethodConstants* constants = nullptr,
    const abc::MethodLookup* methods = nullptr,
    analysis::CapturedValues captures = analysis::CapturedValues::kReject);
}  // namespace abcd::source
#endif
