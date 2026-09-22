#ifndef ABCD_TEST_LEAF_FUNCTION_CHECKS_H_
#define ABCD_TEST_LEAF_FUNCTION_CHECKS_H_
#include "abcd/source/created_function.h"
namespace abcd::test {
void CheckLeafFunctions(const corpus::AbcFile& file, const abc::Header& header,
                        abc::MetadataIndex& index,
                        const abc::MethodLookup& lookup,
                        abc::CodeInfo large_code, bool emit);
}
#endif
