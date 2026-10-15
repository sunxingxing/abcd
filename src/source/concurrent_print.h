#ifndef ABCD_SOURCE_CONCURRENT_PRINT_H_
#define ABCD_SOURCE_CONCURRENT_PRINT_H_
#include "abcd/source/concurrent_directive.h"
namespace abcd::source {
// Synchronous output-syntax scope, inherited by nested class/family printers.
// It observes an already verified attribute; it grants no recovery admission.
// Ark's pinned module grammar requires IdentifierName export aliases. Records
// which emit no concurrent directive retain their existing byte spelling.
class ConcurrentPrint {
 public:
  ConcurrentPrint():previous_(active_){active_=this;}
  ~ConcurrentPrint(){active_=previous_;}
  ConcurrentPrint(const ConcurrentPrint&)=delete;
  static void Directive(const ConcurrentDirective& directive) {
    if(active_&&!active_->method_)active_->method_=directive.Method();
  }
  static void QuotedModuleName(){if(active_)active_->quoted_name_=true;}
  bool HasQuotedModuleName() const{return quoted_name_;}
  std::optional<abc::EntityOffset> Method() const{return method_;}
 private:
  inline static thread_local ConcurrentPrint* active_=nullptr;
  bool quoted_name_=false;
  ConcurrentPrint* previous_;
  std::optional<abc::EntityOffset> method_;
};
}
#endif
