#ifndef ABCD_TEST_CLASS_SELF_ENCLOSURE_CHECKS_H_
#define ABCD_TEST_CLASS_SELF_ENCLOSURE_CHECKS_H_
#include "../../../src/source/class_self_deferred.h"
namespace abcd::test {
void CheckClassSelfEnclosure(const corpus::AbcFile&, const abc::Header&,
                             const abc::MetadataIndex&,
                             const abc::MethodLookup&,
                             const analysis::MethodRoleIndex&,
                             const analysis::ClassMethodSites&,
                             source::FileFunction&,
                             bool expect_tdz_gate = false);
}
#endif
