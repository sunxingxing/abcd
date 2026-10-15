#ifndef ABCD_SOURCE_SHARED_MODULE_H_
#define ABCD_SOURCE_SHARED_MODULE_H_
#include "sendable_metadata.h"
namespace abcd::source {
// This proves the shared *kind*, not OrdinarySynchronous or initialization.
// Entry effects and export stores still require the normal record tree/recovery.
class SharedModuleProof {
 public:
  static std::variant<SharedModuleProof,SharedModuleFailure> Build(
      const RecordModuleContext&,const corpus::AbcFile&,abc::EntityOffset,SourceContract);
  bool Matches(const RecordModuleContext&,const corpus::AbcFile&,SourceContract)const;
  abc::EntityOffset Record()const{return record_;}
 private:
  SharedModuleProof(abc::EntityOffset r,const abc::ModuleInfo* m):record_(r),module_(m){}
  abc::EntityOffset record_;
  const abc::ModuleInfo* module_;
};
}
#endif
