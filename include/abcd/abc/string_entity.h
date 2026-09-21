#ifndef ABCD_ABC_STRING_ENTITY_H_
#define ABCD_ABC_STRING_ENTITY_H_

#include "abcd/abc/metadata_index.h"

namespace abcd::abc {

struct StringBudget {
  // Additional live string buffers, including temporary MUTF-8 bytes.
  // The caller admits this allowance within the whole-process budget.
  std::size_t bytes = 64 * 1024 * 1024;
};

using StringResult = std::variant<std::u16string, MetadataError>;

// Reads a direct string entity offset, NOT a regional pool index. Preserves
// every UTF-16 unit, including lone surrogates and encoded NUL. O(encoded
// bytes) time/space. Synchronous IO primitive; no whole-file or dependency
// loading.
[[nodiscard]] StringResult ReadStringEntity(const corpus::AbcFile& file,
                                            const Header& header,
                                            EntityOffset offset,
                                            StringBudget budget = {});

}  // namespace abcd::abc

#endif  // ABCD_ABC_STRING_ENTITY_H_
