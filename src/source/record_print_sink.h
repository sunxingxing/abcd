#ifndef ABCD_SOURCE_RECORD_PRINT_SINK_H_
#define ABCD_SOURCE_RECORD_PRINT_SINK_H_
#include "print_memory.h"
#include "abcd/source/source_text.h"
#include <cstring>
#include <climits>
#include <streambuf>
namespace abcd::source {
// A counting proof supplies the exact bound. Allocation is charged before new[]
// and the final artifact takes that allocation directly (no string copy).
class RecordPrintSink final:public std::streambuf {
 public:
  RecordPrintSink(PrintMemory& memory,std::size_t bytes):lease_(memory.Reserve(bytes)),size_(bytes),bytes_(std::make_unique<char[]>(bytes)) {
    lease_.Allocated(bytes);setp(bytes_.get(),bytes_.get()+bytes);
  }
  ~RecordPrintSink(){if(bytes_){bytes_.reset();lease_.Freed(size_);}}
  std::size_t Written()const{return static_cast<std::size_t>(pptr()-pbase());}
  SourceText Finish(){
    if(Written()!=size_)throw TreePrintFailure{TreePrintError::kUnsupportedContext};
    lease_.Publish();return SourceText(std::move(bytes_),size_);
  }
 protected:
  std::streamsize xsputn(const char* data,std::streamsize count)override{
    if(count<0||static_cast<std::uintmax_t>(count)>static_cast<std::size_t>(epptr()-pptr()))return 0;
    std::memcpy(pptr(),data,static_cast<std::size_t>(count));
    // pbump takes int even when the streamsize type is wider.
    auto left=count;while(left){auto n=static_cast<int>(std::min<std::streamsize>(left,INT_MAX));pbump(n);left-=n;}
    return count;
  }
  int_type overflow(int_type value)override{return traits_type::eq_int_type(value,traits_type::eof())?traits_type::not_eof(value):traits_type::eof();}
 private:
  PrintMemory::Lease lease_;std::size_t size_;std::unique_ptr<char[]> bytes_;
};
}
#endif
