#ifndef ABCD_SOURCE_OBJECT_FIELD_H_
#define ABCD_SOURCE_OBJECT_FIELD_H_

#include "abcd/analysis/scalar.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::source {
// A proven initialization replacing an existing buffered data property's value.
// Emission currently admits EmptyArray only, never private slots or user code.
struct ObjectField {
  abc::PoolIndex name;
  analysis::ScalarValue value;
};
}  // namespace abcd::source
#endif
