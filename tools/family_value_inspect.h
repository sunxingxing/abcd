#ifndef ABCD_TOOLS_FAMILY_VALUE_INSPECT_H_
#define ABCD_TOOLS_FAMILY_VALUE_INSPECT_H_

#include <nlohmann/json_fwd.hpp>
#include <iosfwd>

#include "abcd/abc/method_lookup.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/source_route.h"

enum class FamilyInspectMode { kValues, kSourceDraft };
// On draft success streams position tables as an object member followed by a
// comma. Caller opens the object, appends returned metadata, then closes it.
// Failure emits no positions. These diagnostics never publish executable source.
nlohmann::json InspectFamilyValues(const abcd::corpus::AbcFile& file,
    const abcd::abc::Header& header, const abcd::abc::MetadataIndex& index,
    const abcd::abc::MethodLookup& lookup, abcd::abc::MethodPosition root,
    abcd::source::RouteInputIdentity identity,
    const abcd::source::RecordModuleContext* modules,
    std::ostream& output,
    FamilyInspectMode mode = FamilyInspectMode::kValues);
#endif
