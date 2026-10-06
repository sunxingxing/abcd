#ifndef ABCD_TEST_CLASS_NAMES_CHECKS_H_
#define ABCD_TEST_CLASS_NAMES_CHECKS_H_
#include "../../../src/source/class_tree.h"
#include <functional>
void CheckClassNames(const std::function<std::shared_ptr<abcd::source::ClassTree>()>&,
    const std::function<abcd::source::FunctionContext()>&);
#endif
