#ifndef ABCD_SOURCE_CLASS_INNER_BINDING_H_
#define ABCD_SOURCE_CLASS_INNER_BINDING_H_
#include "abcd/analysis/value_ir.h"
#include "abcd/source/creator_class_occurrence.h"
namespace abcd::source {
// Original storage of one dynamic class occurrence, never a fictional function
// owner or a plan-local CellId. Native name resolution is checked separately.
struct ClassInnerBinding {
  CreatorClassOccurrence occurrence;
  analysis::OpId creation, allocation;
  analysis::LexicalSlot slot;
  bool operator==(const ClassInnerBinding& other) const {
    return occurrence == other.occurrence && creation == other.creation &&
           allocation == other.allocation && slot.value == other.slot.value;
  }
};
}  // namespace abcd::source
#endif
