#ifndef ABCD_SOURCE_FUNCTION_TEMPLATE_H_
#define ABCD_SOURCE_FUNCTION_TEMPLATE_H_

#include "abcd/source/created_function.h"

namespace abcd::source {
// Private, leaf-only parametric templates. Output requires CaptureBuffer;
// literal bytes alone are NOT executable source. No nested method lookup.
LocalLexicalResult WriteLocalLexicalTemplate(const abc::MethodBody& body,
                                             ScalarNames names,
                                             std::ostream& output,
                                             LocalLexicalBudget budget,
                                             const MethodConstants* constants);
FunctionEmitResult WriteBaseFunctionTemplate(const BaseFunction& function,
                                             const abc::MethodBody& body,
                                             std::ostream& output,
                                             LocalLexicalBudget budget,
                                             const MethodConstants* constants);
FunctionEmitResult WriteArrowFunction(
    const BaseFunction& function, const abc::MethodBody& body,
    std::ostream& output, LocalLexicalBudget budget,
    const MethodConstants* constants, analysis::CapturedValues captured_values);
CreatedFunctionResult WriteCreatedFunctionTemplate(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::DecodedInstruction& instruction,
    std::ostream& output, LocalLexicalBudget budget, abc::StringBudget strings,
    const abc::ModuleInfo* module = nullptr,
    const analysis::MethodRoleIndex* roles = nullptr,
    const GlobalScope* global_scope = nullptr);
CreatedFunctionResult WriteCreatedFunctionTemplate(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const analysis::FunctionCreation& creation,
    std::ostream& output, LocalLexicalBudget budget, abc::StringBudget strings,
    const abc::ModuleInfo* module = nullptr,
    const analysis::MethodRoleIndex* roles = nullptr,
    const GlobalScope* global_scope = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_FUNCTION_TEMPLATE_H_
