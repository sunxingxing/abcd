#ifndef ABCD_SOURCE_COUNTING_SINK_H_
#define ABCD_SOURCE_COUNTING_SINK_H_
#include <streambuf>
#include <limits>
namespace abcd::source {
// Diagnostic sink: runs the real writer, retains no output bytes. It does not
// replace production publication or grant a larger printer budget.
class CountingSink final : public std::streambuf {
 public:
  std::size_t Bytes() const { return bytes_; }
 protected:
  std::streamsize xsputn(const char*,std::streamsize count) override {
    if(count<0||static_cast<std::uintmax_t>(count)>std::numeric_limits<std::size_t>::max()-bytes_)return 0;
    bytes_+=static_cast<std::size_t>(count);return count;
  }
  int_type overflow(int_type value) override {
    if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
    if(bytes_==std::numeric_limits<std::size_t>::max())return traits_type::eof();
    ++bytes_;return value;
  }
 private:
  std::size_t bytes_=0;
};
}
#endif
