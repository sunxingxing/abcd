#ifndef ABCD_SOURCE_SOURCE_IDS_H_
#define ABCD_SOURCE_SOURCE_IDS_H_
#include <cstdint>
#include <compare>
namespace abcd::source {
struct ExprId { std::uint32_t value = 0; auto operator<=>(const ExprId&) const = default; };
struct StmtId { std::uint32_t value = 0; auto operator<=>(const StmtId&) const = default; };
struct ControlId { std::uint32_t value = 0; auto operator<=>(const ControlId&) const = default; };
struct CellId { std::uint32_t value; auto operator<=>(const CellId&) const = default; };
}
#endif
