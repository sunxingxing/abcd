#ifndef ABCD_SOURCE_CLASS_METHOD_H_
#define ABCD_SOURCE_CLASS_METHOD_H_
#include "abcd/abc/class_buffer.h"
#include "abcd/abc/frame_layout.h"
#include "abcd/abc/method_attributes.h"
#include "abcd/source/function_emit_error.h"
#include "abcd/source/function_emission.h"

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
using FunctionEmissionResult =
    std::variant<FunctionEmission, FunctionEmitError, TreePrintError>;
// Value-tree class member emission. Match the selected owner shape, then
// emit the whole member from its verified tree. No fallback or hidden secondary
// self binding: the ABI census proves self/new.target/arguments unobserved.
// Literal-default syntax additionally requires the owning factory's signature
// migration proof; accepting the census layout here is not such a proof.
// Moves ownership into the result so record assembly can retain provenance.
// destination is the caller's counted stream position, not tellp() or a source
// text search. Shape refusal writes nothing; output/resource failure may leave
// a prefix, which the enclosing atomic publisher must discard.
[[nodiscard]] FunctionEmissionResult WriteClassMethod(
    const ClassMethod& method, VerifiedFunction function,
    std::ostream& output, GeneratedOffset destination,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {},
    const GlobalScope* scope = nullptr);
}  // namespace abcd::source
#endif
