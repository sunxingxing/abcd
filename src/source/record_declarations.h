#ifndef ABCD_SOURCE_RECORD_DECLARATIONS_H_
#define ABCD_SOURCE_RECORD_DECLARATIONS_H_

#include "abcd/source/semantic_source.h"

namespace abcd::source {
// Insert declarations at the printer-recorded start of the record body. Every
// span must belong to that body. bytes excludes method maps/slot-use storage
// already charged by SourcePrinter, but includes text and entry-span storage.
// No text is inspected to determine use, insertion position, or provenance.
void InsertRecordDeclarations(PrintedSource& source, ScalarNames names,
                              GeneratedPosition body, std::size_t bytes);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_RECORD_DECLARATIONS_H_
