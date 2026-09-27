#ifndef ABCD_SOURCE_SELECTED_FUNCTION_H_
#define ABCD_SOURCE_SELECTED_FUNCTION_H_

#include "abcd/source/created_function.h"
#include "abcd/source/function_creation_selection.h"
#include "creation_expansion.h"

namespace abcd::source {
CreatedFunctionResult WriteSelectedFunction(
    const FunctionCreationSelection& selection, const analysis::FunctionCreation& creation,
    abc::CodeOffset pc, const MethodConstants& constants, CreationExpansion& expansion,
    std::ostream& output, LocalLexicalBudget budget);
}  // namespace abcd::source
#endif
