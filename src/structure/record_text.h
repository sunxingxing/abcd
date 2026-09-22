#ifndef ABCD_STRUCTURE_RECORD_TEXT_H_
#define ABCD_STRUCTURE_RECORD_TEXT_H_

#include "abcd/abc/debug_info.h"
#include "abcd/abc/method_body.h"
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
void WriteDebugStructure(const RecordText& text, const abc::MethodInfo& method,
                         std::ostream& metadata);
void WriteScopeStructure(const RecordText& text, const abc::MethodInfo& method,
                         const abc::DebugInfo& debug, std::ostream& metadata);
void WriteClassSites(const RecordText& text, const abc::MetadataIndex& index,
                     const abc::MethodLookup& lookup,
                     const abc::MethodInfo& method, std::ostream& metadata,
                     const abc::FrameLayout* frame = nullptr);
void WriteOwnSites(const RecordText& text, const abc::MetadataIndex& index,
                   const abc::MethodInfo& method, const abc::MethodBody& body,
                   std::ostream& metadata, const abc::FrameLayout* frame);
void WriteGlobalSites(const RecordText& text, const abc::MetadataIndex& index,
                      const abc::MethodInfo& method,
                      const abc::MethodBody& body, std::ostream& metadata);
}  // namespace abcd::structure
#endif
