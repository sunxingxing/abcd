#include "abcd/source/string_literal.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
void Expect(std::u16string_view input, std::string_view expected) {
  std::ostringstream output;
  const auto result = abcd::source::WriteStringLiteral(input, output);
  Check(std::get<std::size_t>(result) == expected.size());
  Check(output.str() == expected);
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd::source;
  if (argc == 2 && std::string_view(argv[1]) == "--javascript") {
    std::u16string units;
    for (std::uint32_t unit = 0; unit <= UINT16_MAX; ++unit)
      units.push_back(static_cast<char16_t>(unit));
    std::cout << "const value = ";
    Check(std::holds_alternative<std::size_t>(
        WriteStringLiteral(units, std::cout)));
    std::cout << ";\nif (value.length !== 65536) throw Error('length');\n"
                 "for (let i = 0; i < value.length; ++i) {\n"
                 "  if (value.charCodeAt(i) !== i) throw Error('unit ' + i);\n"
                 "}\n";
    return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  Expect(u"", "\"\"");
  Expect(u"hello", "\"hello\"");
  Expect(u"\"\\'", "\"\\\"\\\\'\"");
  Expect(std::u16string({0, u'9', 0x2028, 0x2029, 0xd800, 0xdfff}),
         "\"\\u00009\\u2028\\u2029\\ud800\\udfff\"");
  Expect(u"\n\r\t", "\"\\u000a\\u000d\\u0009\"");
  Expect(std::u16string({0xd83d, 0xde00}), "\"\\ud83d\\ude00\"");
  for (const std::size_t length : {200U, 201U, 1024U, 8192U})
    Expect(std::u16string(length, u'a'), '"' + std::string(length, 'a') + '"');
  std::ostringstream limited;
  limited << "prefix";
  Check(std::get<EmitError>(WriteStringLiteral(u"ab", limited, {3})) ==
        EmitError::kOutputLimit);
  Check(limited.str() == "prefix");
  Check(std::get<std::size_t>(WriteStringLiteral(u"ab", limited, {4})) == 4);
  std::ostringstream failed;
  failed.setstate(std::ios::badbit);
  Check(std::get<EmitError>(WriteStringLiteral(u"abc", failed)) ==
        EmitError::kWrite);
  failed.exceptions(std::ios::goodbit);
  failed.clear();
  failed.exceptions(std::ios::badbit);
  try {
    failed.setstate(std::ios::badbit);
  } catch (const std::ios_base::failure&) {
  }
  Check(std::get<EmitError>(WriteStringLiteral(u"abc", failed)) ==
        EmitError::kWrite);
}
