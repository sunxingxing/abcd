#ifndef ABCD_SOURCE_RECEIVER_BINDING_H_
#define ABCD_SOURCE_RECEIVER_BINDING_H_

#include "semantic_ast_input.h"

namespace abcd::source {
class CopyOrigins;
// Extend the common native-this binding with a unique super-result local.
// The callable owner supplies the independent initialization/return proof.
void RecoverReceiverBinding(const AstMethod& method, SourceAst& ast,
    std::span<const std::size_t> uses, const CopyOrigins& copies,
    const analysis::RangeDerivedConstructor& initialized);
}  // namespace abcd::source
#endif
