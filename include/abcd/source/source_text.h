#ifndef ABCD_SOURCE_SOURCE_TEXT_H_
#define ABCD_SOURCE_SOURCE_TEXT_H_
#include <memory>
#include <ostream>
#include <string_view>
#include <utility>
namespace abcd::source {
// Exact-size record output. Ownership transfers without a second string
// allocation; the record writer reserves these bytes before constructing it.
class SourceText {
 public:
  SourceText()=default;
  SourceText(std::unique_ptr<char[]> bytes,std::size_t size)noexcept:bytes_(std::move(bytes)),size_(size){}
  SourceText(SourceText&& other)noexcept:bytes_(std::move(other.bytes_)),size_(std::exchange(other.size_,0)){}
  SourceText& operator=(SourceText&& other)noexcept {
    if(this!=&other){bytes_=std::move(other.bytes_);size_=std::exchange(other.size_,0);}return *this;
  }
  std::string_view View()const{return {bytes_?bytes_.get():"",size_};}
  const char* data()const{return View().data();}
  std::size_t size()const{return size_;}
  std::size_t find(std::string_view text,std::size_t pos=0)const{return View().find(text,pos);}
  friend std::ostream& operator<<(std::ostream& out,const SourceText& text){return out<<text.View();}
 private:
  std::unique_ptr<char[]> bytes_;std::size_t size_=0;
};
}
#endif
