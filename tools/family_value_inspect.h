#ifndef ABCD_TOOLS_FAMILY_VALUE_INSPECT_H_
#define ABCD_TOOLS_FAMILY_VALUE_INSPECT_H_

#include <nlohmann/json_fwd.hpp>

#include "abcd/abc/method_lookup.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/source_route.h"

nlohmann::json InspectFamilyValues(const abcd::corpus::AbcFile& file,
    const abcd::abc::Header& header, const abcd::abc::MetadataIndex& index,
    const abcd::abc::MethodLookup& lookup, abcd::abc::MethodPosition root,
    abcd::source::RouteInputIdentity identity,
    const abcd::source::RecordModuleContext* modules);
#endif
