#include "plan/fingerprint.hpp"

namespace repd {

Result<std::string> fingerprint(LuaHost& host, const Value& record) {
  auto v = host.call("fingerprint", {record});
  if (!v) return std::unexpected(v.error());
  return v->as_string();
}

}  // namespace repd
