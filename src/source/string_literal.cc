#include "abcd/source/string_literal.h"

#include <array>
#include "tree_print_trace.h"
#include <cstdint>
#include <ostream>

namespace abcd::source {
namespace {
constexpr char16_t kFirstPrintable = 0x20;
constexpr char16_t kLastPrintable = 0x7e;
constexpr std::size_t kQuoteBytes = 2;
constexpr std::size_t kPlainBytes = 1;
constexpr std::size_t kShortEscapeBytes = 2;
constexpr std::size_t kUnicodeEscapeBytes = 6;
constexpr unsigned kHexDigitsPerUnit = 4;
constexpr unsigned kHexBits = 4;
constexpr unsigned kHexMask = 0xf;
constexpr unsigned kTopHexShift = 12;
constexpr std::string_view kHexDigits = "0123456789abcdef";

std::size_t EncodedSize(char16_t unit) {
  if (unit == u'"' || unit == u'\\') return kShortEscapeBytes;
  return unit >= kFirstPrintable && unit <= kLastPrintable
             ? kPlainBytes
             : kUnicodeEscapeBytes;
}
}  // namespace

std::variant<std::size_t, EmitError> WriteStringLiteral(
    std::u16string_view value, std::ostream& output, EmitBudget budget) {
  if (budget.bytes < kQuoteBytes) {PrintCharge(TreePrintBudgetKind::kOutputBytes,kQuoteBytes,budget.bytes,budget.bytes);return EmitError::kOutputLimit;}
  std::size_t bytes = kQuoteBytes;
  for (const auto unit : value) {
    const auto width = EncodedSize(unit);
    if (width > budget.bytes - bytes) {PrintCharge(TreePrintBudgetKind::kOutputBytes,width,budget.bytes-bytes,budget.bytes,bytes);return EmitError::kOutputLimit;}
    bytes += width;
  }
  try {
    constexpr std::size_t kBufferBytes = 1024;
    std::array<char, kBufferBytes> buffer{};
    std::size_t used = 0;
    const auto append = [&](char byte) {
      buffer[used++] = byte;
      if (used == buffer.size()) {
        output.write(buffer.data(), static_cast<std::streamsize>(used));
        used = 0;
      }
    };
    append('"');
    for (const auto unit : value) {
      switch (EncodedSize(unit)) {
        case kPlainBytes:
          append(static_cast<char>(unit));
          break;
        case kShortEscapeBytes:
          append('\\');
          append(static_cast<char>(unit));
          break;
        default:
          // Fixed-width escapes avoid octal ambiguities, source line breaks,
          // encoding dependencies and accidental surrogate normalization.
          append('\\');
          append('u');
          for (unsigned digit = 0; digit < kHexDigitsPerUnit; ++digit)
            append(kHexDigits[(static_cast<std::uint16_t>(unit) >>
                               (kTopHexShift - digit * kHexBits)) &
                              kHexMask]);
          break;
      }
      if (!output) return EmitError::kWrite;
    }
    append('"');
    output.write(buffer.data(), static_cast<std::streamsize>(used));
    if (!output) return EmitError::kWrite;
    return bytes;
  } catch (const std::ios_base::failure&) {
    return EmitError::kWrite;
  }
}

}  // namespace abcd::source
