#ifndef ABCD_SOURCE_RECORD_LEXICAL_H_
#define ABCD_SOURCE_RECORD_LEXICAL_H_

#include <cstddef>
#include <vector>

#include "abcd/abc/method_body.h"
#include "abcd/analysis/lexical.h"
#include "record_value.h"

namespace abcd::source {
// Only for the record interpreter's sequential environment operations. Its
// admitted branch regions contain no lexical operations; backedges and outer
// captures remain rejected. Never reuse these symbolic cells once a created
// callable is allowed to observe/mutate its creation environment.
class RecordLexicals {
 public:
  bool Apply(const abc::BodyInstruction& site, std::size_t frame_size,
             RecordValue& accumulator, std::size_t& bytes);

 private:
  RecordValue& Cell(analysis::LexicalAddress address, abc::CodeOffset pc);
  struct Frame {
    std::size_t base;
    std::size_t identity;
  };
  std::vector<Frame> frames_;
  std::vector<RecordValue> cells_;
  std::size_t next_identity_ = 0;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_RECORD_LEXICAL_H_
