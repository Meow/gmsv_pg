#include <pqxx/internal/callgate.hxx>

namespace pqxx
{
namespace internal
{
namespace gate
{
class PQXX_PRIVATE result_sql_cursor : callgate<const result>
{
  friend class pqxx::internal::sql_cursor;

  result_sql_cursor(reference x) : super(x) {}

  const char *cmd_status() const noexcept { return home().cmd_status(); }
};
} // namespace pqxx::internal::gate
} // namespace pqxx::internal
} // namespace pqxx
