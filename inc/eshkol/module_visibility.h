#ifndef ESHKOL_MODULE_VISIBILITY_H
#define ESHKOL_MODULE_VISIBILITY_H

#include <eshkol/eshkol.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace eshkol {

// asts must be fresh parser output: renamed name slots are parser-owned
// new[] buffers. Run this before macro expansion, which may introduce
// differently owned strings.
void rename_private_symbols(std::vector<eshkol_ast_t>& asts,
                            const std::string& module_name,
                            const std::set<std::string>& exports);

}  // namespace eshkol

#endif
