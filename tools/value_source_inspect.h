#ifndef ABCD_TOOLS_VALUE_SOURCE_INSPECT_H_
#define ABCD_TOOLS_VALUE_SOURCE_INSPECT_H_

#include <iosfwd>

#include "abcd/abc/metadata_index.h"
#include "abcd/source/source_route.h"

enum class ValueSourceScope { kMember, kRecordSlice };
int InspectValueSource(const abcd::corpus::AbcFile& file,
    const abcd::abc::Header& header, const abcd::abc::MetadataIndex& index,
    const abcd::abc::MethodInfo& method, abcd::source::RouteInputIdentity identity,
    std::ostream& output, ValueSourceScope scope = ValueSourceScope::kMember);
#endif
