#include <zlib.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

#include "abcd/abc/header.h"

namespace {
constexpr std::size_t kChecksumOffset = 8;
constexpr std::size_t kVersionOffset = 12;
constexpr std::size_t kFileSizeOffset = 16;
constexpr std::size_t kClassCountOffset = 28;
constexpr std::size_t kClassTableOffset = 32;
constexpr std::size_t kRegionCountOffset = 52;
constexpr std::size_t kRegionTableOffset = 56;
constexpr std::size_t kRegionBytes = 40;
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kRegionOffset =
    abcd::abc::kDynamicHeaderSize + kWordBytes;
constexpr std::uint32_t kClassOffset = kRegionOffset + kRegionBytes;
enum class FixtureKind {
  kEmptyRecord,
  kMethod,
  kFrameMethod,
  kInvalidMethod,
  kManyInvalidMethods,
  kFallthroughMethod,
  kMixedMethods,
  kMixedMethodsReversed,
  kExceptionMethod,
  kReferenceMethod,
  kSourceMethod,
  kSourceString,
  kSourceNameBudget,
  kEnvironmentMethod,
  kCaptureSummary,
  kEnvironmentConflict,
  kCreationMethod,
  kCreationNonmethod,
  kCreationUnreachable,
  kSourceLoop,
  kSourceException,
  kInvalidReference,
  kModuleRecord,
  kBundleModule
};

void WriteWord(std::span<std::uint8_t> bytes, std::size_t offset,
               std::uint32_t value) {
  for (std::size_t i = 0; i < kWordBytes; ++i)
    bytes[offset + i] = value >> (i * kByteBits);
}

bool Write(const std::filesystem::path& path, std::vector<std::uint8_t> bytes,
           abcd::abc::AbcVersion version) {
  constexpr std::array<std::uint8_t, 8> kMagic{'P', 'A', 'N', 'D',
                                               'A', 0,   0,   0};
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
  std::copy(version.bytes.begin(), version.bytes.end(),
            bytes.begin() + kVersionOffset);
  WriteWord(bytes, kFileSizeOffset, bytes.size());
  WriteWord(
      bytes, kChecksumOffset,
      adler32(1, bytes.data() + kVersionOffset, bytes.size() - kVersionOffset));
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  output.close();
  return !output.fail();
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) return EXIT_FAILURE;
  FixtureKind kind = FixtureKind::kEmptyRecord;
  if (argc == 3) {
    const std::string_view option(argv[2]);
    if (option == "--method")
      kind = FixtureKind::kMethod;
    else if (option == "--frame-method")
      kind = FixtureKind::kFrameMethod;
    else if (option == "--invalid-method")
      kind = FixtureKind::kInvalidMethod;
    else if (option == "--many-invalid-methods")
      kind = FixtureKind::kManyInvalidMethods;
    else if (option == "--fallthrough-method")
      kind = FixtureKind::kFallthroughMethod;
    else if (option == "--mixed-methods")
      kind = FixtureKind::kMixedMethods;
    else if (option == "--mixed-methods-reversed")
      kind = FixtureKind::kMixedMethodsReversed;
    else if (option == "--exception-method")
      kind = FixtureKind::kExceptionMethod;
    else if (option == "--reference-method")
      kind = FixtureKind::kReferenceMethod;
    else if (option == "--source-method")
      kind = FixtureKind::kSourceMethod;
    else if (option == "--source-string")
      kind = FixtureKind::kSourceString;
    else if (option == "--source-name-budget")
      kind = FixtureKind::kSourceNameBudget;
    else if (option == "--environment-method")
      kind = FixtureKind::kEnvironmentMethod;
    else if (option == "--capture-summary")
      kind = FixtureKind::kCaptureSummary;
    else if (option == "--environment-conflict")
      kind = FixtureKind::kEnvironmentConflict;
    else if (option == "--creation-method")
      kind = FixtureKind::kCreationMethod;
    else if (option == "--creation-nonmethod")
      kind = FixtureKind::kCreationNonmethod;
    else if (option == "--creation-unreachable")
      kind = FixtureKind::kCreationUnreachable;
    else if (option == "--source-loop")
      kind = FixtureKind::kSourceLoop;
    else if (option == "--source-exception")
      kind = FixtureKind::kSourceException;
    else if (option == "--invalid-reference")
      kind = FixtureKind::kInvalidReference;
    else if (option == "--module-record")
      kind = FixtureKind::kModuleRecord;
    else if (option == "--bundle-module")
      kind = FixtureKind::kBundleModule;
    else
      return EXIT_FAILURE;
  }
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  // A local class with no members. Encoded UTF-16 length includes ASCII flag;
  // superclass, flags, field count, method count and terminating tag are zero.
  constexpr std::array<std::uint8_t, 16> kClass{
      13, 'L', 'd', 'e', 'm', 'o', ';', 0, 0, 0, 0, 0, 0, 0, 0, 0};
  std::vector<std::uint8_t> dynamic(kClassOffset);
  dynamic.insert(dynamic.end(), kClass.begin(), kClass.end());
  if (kind == FixtureKind::kModuleRecord || kind == FixtureKind::kBundleModule) {
    if (kind == FixtureKind::kBundleModule) {
      dynamic.resize(kClassOffset);
      constexpr std::string_view kDescriptor = "L_ESModuleRecord;";
      dynamic.push_back((kDescriptor.size() << 1) | 1);
      dynamic.insert(dynamic.end(), kDescriptor.begin(), kDescriptor.end());
      dynamic.insert(dynamic.end(), {0, 0, 0, 0, 0, 0, 2, 0, 0});
    } else {
      dynamic[kClassOffset + 13] = 2;
    }
    const auto first_field = dynamic.size();
    dynamic.insert(dynamic.end(), {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0});
    const auto second_field = dynamic.size();
    dynamic.insert(dynamic.end(), {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0});
    const auto name = [&](std::string_view text) {
      const auto offset = dynamic.size();
      dynamic.push_back(static_cast<std::uint8_t>((text.size() << 1) | 1));
      dynamic.insert(dynamic.end(), text.begin(), text.end());
      dynamic.push_back(0);
      return offset;
    };
    const auto common_name = name(kind == FixtureKind::kBundleModule ? "ignored" : "isCommonjs");
    const auto module_name = name(kind == FixtureKind::kBundleModule ? "entry.abc" : "moduleRecordIdx");
    WriteWord(dynamic, first_field + 4, common_name);
    WriteWord(dynamic, second_field + 4, module_name);
    const auto module = dynamic.size();
    dynamic[second_field + 10] = (module & 0x7f) | 0x80;
    dynamic[second_field + 11] = module >> 7;
    dynamic.resize(dynamic.size() + 7 * kWordBytes);  // Empty module tables.
  } else if (kind != FixtureKind::kEmptyRecord) {
    constexpr std::size_t kMethodCountInClass = 14;
    constexpr std::size_t kNameOffsetInMethod = 4;
    constexpr std::size_t kCodeOffsetInMethod = 10;
    // u16 owner, absent proto, name offset, flags, CODE + offset, end.
    constexpr std::array<std::uint8_t, 15> kMethod{0, 0, 0xff, 0xff, 0, 0, 0, 0,
                                                   0, 1, 0,    0,    0, 0, 0};
    constexpr std::uint8_t kDiagnosticOverflowCount = 65;
    const bool mixed = kind == FixtureKind::kMixedMethods ||
                       kind == FixtureKind::kMixedMethodsReversed;
    const std::uint8_t method_count = kind == FixtureKind::kManyInvalidMethods
                                          ? kDiagnosticOverflowCount
                                      : mixed ? 2
                                              : 1;
    const auto method_offset = dynamic.size();
    dynamic[kClassOffset + kMethodCountInClass] = method_count;
    for (std::uint8_t i = 0; i < method_count; ++i)
      dynamic.insert(dynamic.end(), kMethod.begin(), kMethod.end());
    const auto name_offset = dynamic.size();
    dynamic.insert(dynamic.end(), {9, 'm', 'a', 'i', 'n', 0});
    // Zero frame slots, one instruction, no tries. 0x65 is returnundefined;
    // 0xff is unassigned in the pinned dynamic ISA, not a static-file marker.
    for (std::uint8_t i = 0; i < method_count; ++i) {
      const auto code_offset = dynamic.size();
      const bool fallthrough =
          kind == FixtureKind::kFallthroughMethod ||
          (mixed && ((i == 0) == (kind == FixtureKind::kMixedMethodsReversed)));
      if (kind == FixtureKind::kReferenceMethod ||
          kind == FixtureKind::kSourceMethod ||
          kind == FixtureKind::kSourceString ||
          kind == FixtureKind::kInvalidReference) {
        if (kind == FixtureKind::kSourceString) {
          // A successful source path must resolve the regional string slot.
          dynamic.insert(dynamic.end(), {0, 3, 4, 0, 0x3e, 0, 0, 0x64});
        } else {
          // lda.str id16, createarraywithbuffer imm8/id16, returnundefined.
          dynamic.insert(dynamic.end(),
                         {0, 0, 8, 0, 0x3e,
                          static_cast<std::uint8_t>(
                              kind == FixtureKind::kInvalidReference ? 2 : 0),
                          0, 0x06, 0, 1, 0, 0x65});
        }
        const auto literal_offset = dynamic.size();
        dynamic.insert(dynamic.end(), {2, 0, 0, 0, 0x05, 0, 0, 0, 0});
        WriteWord(dynamic, literal_offset + 5, name_offset);
        const auto pool_offset = dynamic.size();
        dynamic.resize(dynamic.size() + 2 * kWordBytes);
        WriteWord(dynamic, pool_offset, name_offset);
        WriteWord(dynamic, pool_offset + kWordBytes, literal_offset);
        WriteWord(dynamic, kRegionOffset + 4 * kWordBytes, 2);
        WriteWord(dynamic, kRegionOffset + 5 * kWordBytes, pool_offset);
      } else if (kind == FixtureKind::kCreationUnreachable) {
        // No pool is needed: the creation is normal-unreachable, not bound.
        dynamic.insert(dynamic.end(),
                       {0, 3, 8, 0, 0x4d, 7, 0x33, 0, 0, 0, 2, 0x65});
      } else if (kind == FixtureKind::kCreationMethod ||
                 kind == FixtureKind::kCreationNonmethod) {
        // Same target created before and after popping the new environment.
        dynamic.insert(dynamic.end(), {0, 3, 14, 0, 0x09, 1,
            0x33, 0, 0, 0, 2, 0x69, 0x33, 0, 0, 0, 3, 0x65});
        const auto pool_offset = dynamic.size();
        dynamic.resize(dynamic.size() + kWordBytes);
        WriteWord(dynamic, pool_offset,
                  kind == FixtureKind::kCreationMethod ? method_offset : name_offset);
        WriteWord(dynamic, kRegionOffset + 4 * kWordBytes, 1);
        WriteWord(dynamic, kRegionOffset + 5 * kWordBytes, pool_offset);
      } else if (kind == FixtureKind::kEnvironmentMethod) {
        dynamic.insert(dynamic.end(), {0, 3, 10, 0, 0x09, 2, 0x3c, 0,
                                       0x3d, 0x10, 0x69, 0x3c, 0, 0x65});
      } else if (kind == FixtureKind::kCaptureSummary) {
        dynamic.insert(dynamic.end(),
                       {0, 3, 7, 0, 0x3c, 0, 0x3c, 0, 0x3c, 0x10, 0x65});
      } else if (kind == FixtureKind::kEnvironmentConflict) {
        dynamic.insert(dynamic.end(), {0, 3, 14, 0, 0x09, 1, 0x02, 0x4f, 7,
                                       0x09, 1, 0xd5, 0x4d, 3, 0xd5, 0x3c, 0, 0x65});
      } else if (kind == FixtureKind::kSourceLoop) {
        dynamic.insert(dynamic.end(), {0, 3, 2, 0, 0x4d, 0});
      } else if (kind == FixtureKind::kSourceNameBudget) {
        // ULEB 2^22 local registers: valid frame, exceeds the name budget.
        // No large fixture allocation or register operand is necessary.
        constexpr std::array<std::uint8_t, 8> kLargeFrame{
            0x80, 0x80, 0x80, 0x02, 3, 1, 0, 0x65};
        dynamic.insert(dynamic.end(), kLargeFrame.begin(), kLargeFrame.end());
      } else if (kind == FixtureKind::kExceptionMethod ||
                 kind == FixtureKind::kSourceException) {
        // Four prefix ULEBs; [ldundefined, throw, returnundefined,
        // returnundefined]. Try [0,3), first typed catch at [3,4), then
        // catch-all at [4,5).
        dynamic.insert(dynamic.end(), {0, 0, 5, 1, 0x00, 0xfe, 0x00, 0x65, 0x65,
                                       0, 3, 2, 2, 3, 1, 0, 4, 1});
      } else {
        dynamic.insert(
            dynamic.end(),
            {0, 0, 1, 0,
             static_cast<std::uint8_t>(kind == FixtureKind::kMethod ? 0x65
                                       : fallthrough                ? 0xd5
                                                                    : 0xff)});
      }
      const auto offset = method_offset + i * kMethod.size();
      if (kind == FixtureKind::kSourceMethod ||
          kind == FixtureKind::kSourceException)
        dynamic[code_offset + 1] = 3;
      if (kind == FixtureKind::kFrameMethod) {
        dynamic[code_offset + 1] = 3;  // Missing annotation enables 3 hidden slots.
        dynamic[code_offset + 4] = 0x65;
      }
      WriteWord(dynamic, offset + kNameOffsetInMethod, name_offset);
      WriteWord(dynamic, offset + kCodeOffsetInMethod, code_offset);
    }
  }
  WriteWord(dynamic, kClassCountOffset, 1);
  WriteWord(dynamic, kClassTableOffset, abcd::abc::kDynamicHeaderSize);
  WriteWord(dynamic, abcd::abc::kDynamicHeaderSize, kClassOffset);
  WriteWord(dynamic, kRegionCountOffset, 1);
  WriteWord(dynamic, kRegionTableOffset, kRegionOffset);
  WriteWord(dynamic, kRegionOffset, kClassOffset);
  WriteWord(dynamic, kRegionOffset + kWordBytes, dynamic.size());
  WriteWord(dynamic, kRegionOffset + 2 * kWordBytes, 1);
  WriteWord(dynamic, kRegionOffset + 3 * kWordBytes,
            abcd::abc::kDynamicHeaderSize);
  if (!Write(root / "dynamic.abc", std::move(dynamic), {{12, 0, 6, 0}}) ||
      !Write(root / "static.abc",
             std::vector<std::uint8_t>(abcd::abc::kStaticHeaderSize),
             {{0, 1, 0, 7}}))
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
