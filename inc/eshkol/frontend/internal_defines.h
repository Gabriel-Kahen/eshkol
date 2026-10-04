/*
 * Copyright (C) tsotchke
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef ESHKOL_FRONTEND_INTERNAL_DEFINES_H
#define ESHKOL_FRONTEND_INTERNAL_DEFINES_H

#include <eshkol/eshkol.h>
#include <eshkol/frontend/node_identity.h>
#include <vector>

namespace eshkol {

/* Reuse the parser's letrec* construction after macro expansion. This does not
 * grant expression-position definitions a lexical body. */
eshkol_ast_t transformExpandedBodyDefines(const std::vector<eshkol_ast_t>& expressions);

/* True only for a letrec* created by the parser's body-definition transform.
 * Node identity survives MacroExpander's AST copies; shape/name does not prove
 * that an arbitrary source-written letrec* has this origin. */
bool isParserBodyLetrec(const eshkol_ast_t& ast,
                       eshkol_node_id_t* owner_id = nullptr);

/* Check and normalize definition positions after syntax-rules expansion, before
 * any LLVM IR is emitted. Returns false for unsupported mixed/ambiguous forms. */
bool normalizeExpandedDefinitions(std::vector<eshkol_ast_t>& top_level_forms);

} // namespace eshkol

#endif
