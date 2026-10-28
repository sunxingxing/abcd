#ifndef ABCD_SOURCE_CLASS_INITIALIZER_CALL_H_
#define ABCD_SOURCE_CLASS_INITIALIZER_CALL_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
// A class static-field initializer: its DEFINEMETHOD on the class and the
// single zero-argument call with the class as receiver. In phased graphs the
// receiver is the actual call operand, a pure register copy of the class.
struct ClassInitializerCall {
  analysis::OpId creation;
  analysis::OpId method;
  analysis::OpId call;
  analysis::ValueId receiver;
  bool operator==(const ClassInitializerCall&) const = default;
};
}  // namespace abcd::source
#endif
