#ifndef ABCD_SOURCE_CLASS_METHOD_H_
#define ABCD_SOURCE_CLASS_METHOD_H_
#include "abcd/abc/class_buffer.h"
#include "abcd/source/base_function.h"

namespace abcd::source {
// FunctionTemplate/Method kind at member materialization, NOT the file flag.
enum class EffectiveMethodKind { kUnknown, kOrdinary };
struct ClassMethod {
  abc::ClassMethod member;
  abc::MethodAttributes attributes;
  abc::FrameLayout frame;
  abc::ClassPlacement placement;
  std::u16string_view key;
  std::string_view self_binding;
  EffectiveMethodKind effective_kind = EffectiveMethodKind::kUnknown;
};
// Native ordinary method, not a constructible function expression. Caller
// proves class-buffer membership, target/ABI, runtime name equal to key, and
// an enclosing private immutable self_binding holding this exact method before
// it can be invoked. Reading Class.prototype[key] at invocation is NOT valid:
// that property is mutable. The body cannot read unresolved outer captures.
// Only ordinary templates with initial NONE AND proven effective ordinary kind
// are admitted. v7.0 DefineFunctionTemplate mutates shared MethodLiteral kind;
// an earlier conflicting template may change NONE before this site's creation.
// Caller must prove runtime NORMAL_FUNCTION, including cross-role conflicts;
// initial NONE alone is not evidence. Unknown evidence is rejected by default.
// Placement/key descriptor and reserved-property compatibility are caller
// obligations. Does not emit home-object operations, async/generator/accessors.
// Simple formal count, preflight, budgets and publication match BaseFunction.
[[nodiscard]] FunctionEmitResult WriteClassMethod(
    const ClassMethod& method, const abc::MethodBody& body,
    std::ostream& output, LocalLexicalBudget budget = {},
    const MethodConstants* constants = nullptr);
}  // namespace abcd::source
#endif
