#ifndef ABCD_STRUCTURE_RECORD_TEXT_H_
#define ABCD_STRUCTURE_RECORD_TEXT_H_

#include "abcd/structure/record_writer.h"

namespace abcd::structure {
// These are wire/source boundaries only. All dispatch uses typed metadata.
class RecordText {
 public:
  RecordText(const corpus::AbcFile& file, const abc::Header& header,
             StructureBudget budget)
      : file(file), header(header), budget(budget) {}
  std::u16string Read(abc::EntityOffset offset) const;
  void String(std::ostream& out, std::u16string_view value) const;
  void String(std::ostream& out, abc::EntityOffset offset) const;
  const corpus::AbcFile& file;
  const abc::Header& header;
  StructureBudget budget;
};
void WriteModuleStructure(const RecordText& text, const abc::ModuleInfo& module,
                          std::ostream& metadata, std::ostream& declarations);
}  // namespace abcd::structure
#endif
