#ifndef ABCD_SOURCE_BINDING_STORAGE_H_
#define ABCD_SOURCE_BINDING_STORAGE_H_
#include "binding_initializer.h"
namespace abcd::source {
inline std::size_t BindingStorage(const BindingInitializers& plan) {
  // All owned binding-plan payloads are flat vectors (literal text is an ID).
  // Count capacity, not size; adding an owning field requires adding it here.
  const auto bytes=[]<class T>(const std::vector<T>& values){return values.capacity()*sizeof(T);};
  return bytes(plan.statements)+bytes(plan.omitted)+bytes(plan.statement_values)+
      bytes(plan.forwarded_values)+bytes(plan.loop_declarations)+bytes(plan.primitives)+
      bytes(plan.literals)+bytes(plan.read_literals)+bytes(plan.read_bindings)+bytes(plan.read_sources)+
      bytes(plan.aliases)+bytes(plan.storage)+bytes(plan.undefined_stores)+bytes(plan.declaration_values)+
      bytes(plan.declaration_completions)+bytes(plan.replacements)+bytes(plan.transports)+
      bytes(plan.evaluations)+bytes(plan.normal_stores)+bytes(plan.entry_counts)+bytes(plan.child_counts);
}
}
#endif
