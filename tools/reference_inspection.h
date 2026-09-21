#ifndef ABCD_TOOLS_REFERENCE_INSPECTION_H_
#define ABCD_TOOLS_REFERENCE_INSPECTION_H_

#include "abcd/abc/method_body.h"
#include "method_inspection.h"

namespace abcd::tools {
std::optional<nlohmann::json> InspectReferences(const corpus::AbcFile& file,
                                                const abc::Header& header,
                                                const abc::MetadataIndex& index,
                                                const abc::MethodInfo& method,
                                                const abc::MethodBody& body,
                                                MethodCounts* counts);
}
#endif
