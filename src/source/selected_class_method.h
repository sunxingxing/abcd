#ifndef ABCD_SOURCE_SELECTED_CLASS_METHOD_H_
#define ABCD_SOURCE_SELECTED_CLASS_METHOD_H_

#include "abcd/source/class_method_selection.h"
#include "class_emission.h"
#include "creation_expansion.h"

namespace abcd::source {
// Throws ClassFailure on any selected-route refusal. The caller must not try
// Body()/legacy bindings after entering this function.
std::size_t WriteSelectedClassMethod(
    const ClassMethodSelection& selection, const corpus::AbcFile& file,
    const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const abc::ClassProperty& property, const ClassMemberTarget& target,
    std::u16string_view key, CreationExpansion& expansion,
    std::ostream& output, LocalLexicalBudget budget, const GlobalScope* scope);
}  // namespace abcd::source
#endif
