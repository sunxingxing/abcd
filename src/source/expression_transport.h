#ifndef ABCD_SOURCE_EXPRESSION_TRANSPORT_H_
#define ABCD_SOURCE_EXPRESSION_TRANSPORT_H_
#include "primitive_value.h"
namespace abcd::source {
// The source occurrence no longer evaluates this RHS: its unique actual read
// evaluates it instead. The discarded result projects to an inert primitive.
// Shared value-use/motion/exception/lifetime proof must precede publication.
struct ExpressionTransport {ExprId value;PrimitiveId replacement;bool operator==(const ExpressionTransport&) const = default;};
}
#endif
