#include "internal.h"

TekSynNode* TekGenSyn_alloc_node(TekWorker* w, TekSynNodeKind kind, uint32_t token_idx) {
	TekSynNode* node = &w->gen_syn.nodes[w->gen_syn.nodes_next_idx];
	w->gen_syn.nodes_next_idx += 1;

	//
	// data is zeroed automatically from the OS when the memory gets committed.
	// just initialize the fields
	node->kind = kind;
	node->token_idx = token_idx;
	return node;
}

static inline TekToken TekGenSyn_token_peek(TekWorker* w) {
	return w->gen_syn.tokens[w->gen_syn.token_idx];
}

static inline TekToken TekGenSyn_token_peek_ahead(TekWorker* w, uint32_t amount) {
	return w->gen_syn.tokens[w->gen_syn.token_idx + amount];
}

static inline TekToken TekGenSyn_token_move_next(TekWorker* w) {
	w->gen_syn.token_idx += 1;
	return w->gen_syn.tokens[w->gen_syn.token_idx];
}

static inline TekToken TekGenSyn_token_move_ahead(TekWorker* w, uint32_t amount) {
	w->gen_syn.token_idx += amount;
	return w->gen_syn.tokens[w->gen_syn.token_idx];
}

static inline const TekValue* TekGenSyn_token_value_take(TekWorker* w) {
	const TekValue* v = &w->gen_syn.token_values[w->gen_syn.token_value_idx];
	w->gen_syn.token_value_idx += 1;
	return v;
}

#define TekGenSyn_error_token(w, error_kind) \
	{ \
		TekError* _e = TekCompiler_error_add(w->c, error_kind); \
		_e->args[0].file_id = w->gen_syn.file_id; \
		_e->args[0].token_idx = w->gen_syn.token_idx; \
	}

#define TekGenSyn_ensure_token(w, token, expected_token, error_kind) \
	if (token != expected_token) { \
		TekGenSyn_error_token(w, error_kind); \
		return NULL; \
	}

//
// new lines are combined in the lexer, so we only have to check once.
#define TekGenSyn_skip_new_lines(w, token) \
	if (token == '\n') { \
		token = TekGenSyn_token_move_next(w); \
	}

void TekGenSyn_abort_unexpected_token(TekWorker* w, TekToken token, TekToken expected_token, const char* file, int line, const char* func) {
	char expected_token_name[128];
	TekToken_as_string(expected_token, expected_token_name, sizeof(expected_token_name));

	char token_name[128];
	TekToken_as_string(token, token_name, sizeof(token_name));

	_tek_abort(file, line, func, NULL, "code flow should have ensured a '%s' token but got '%s'", expected_token_name, token_name);
}

#define TekGenSyn_assert_token(w, token, expected_token) \
	if (token != expected_token) { \
		TekGenSyn_abort_unexpected_token(w, token, expected_token, __FILE__, __LINE__, __func__); \
	}

void _TekGenSyn_assert_tokens(TekWorker* w, TekToken token, TekToken* expected_tokens, uint32_t expected_tokens_count, const char* file, int line, const char* func) {
	for (uint32_t i = 0; i < expected_tokens_count; i += 1) {
		if (expected_tokens[i] == token) return;
	}

	char expected_names[256] = {0};
	uint32_t expected_names_idx = 0;
	for (uint32_t i = 0; i < expected_tokens_count; i += 1) {
		expected_names[expected_names_idx] = '\'';
		expected_names_idx += 1;

		TekToken_as_string(expected_tokens[i], expected_names, sizeof(expected_names) - expected_names_idx);

		expected_names[expected_names_idx] = '\'';
		expected_names_idx += 1;

		if (i + 1 < expected_tokens_count) {
			expected_names[expected_names_idx] = ',';
			expected_names_idx += 1;
			expected_names[expected_names_idx] = ' ';
			expected_names_idx += 1;
		}
	}

	char token_name[256] = {0};
	TekToken_as_string(token, token_name, sizeof(token_name));
	_tek_abort(file, line, func, NULL, "code flow should have ensured one of these %s tokens but got '%s'", expected_names, token_name);
}

#define TekGenSyn_assert_tokens(w, token, ...) \
	{ \
		TekToken _expected_tokens[] = { __VA_ARGS__ }; \
		_TekGenSyn_assert_tokens(w, token, _expected_tokens, sizeof(_expected_tokens) / sizeof(*_expected_tokens), __FILE__, __LINE__, __func__); \
	}


TekBool TekGenSyn_gen_file(TekWorker* w, TekFileId file_id) {
	TekFile* file = TekCompiler_file_get(w->c, file_id);
	w->gen_syn.nodes = TekFile_syntax_tree_nodes(file);
	w->gen_syn.nodes_next_idx = 0;
	w->gen_syn.tokens = TekFile_tokens(file);
	w->gen_syn.token_values = TekFile_token_values(file);
	w->gen_syn.token_idx = 0;
	w->gen_syn.file_id = file_id;

	TekSynNode* mod = TekGenSyn_gen_mod(w, 0, tek_true);
	/*
	//
	// if no errors occurred, then queue a job to generate a semantic tree for the whole file.
	if (mod) {
		TekJob* j = TekCompiler_job_queue(w->c, TekJobType_gen_sem_file);
		j->file_id = file_id;
	}
	*/
	return mod != NULL;
}

TekSynNode* TekGenSyn_gen_mod(TekWorker* w, uint32_t token_idx, TekBool is_file_root) {
	TekSynNode* node = TekGenSyn_alloc_node(w, TekSynNodeKind_mod, token_idx);
	TekToken token = TekGenSyn_token_peek(w);

	if (!is_file_root) {
		TekGenSyn_ensure_token(w, token, '{', TekErrorKind_gen_syn_mod_must_have_impl);
		token = TekGenSyn_token_move_next(w);
	}

	TekGenSyn_skip_new_lines(w, token);
	TekSynNode* first_entry = NULL;
	TekSynNode* prev_entry = NULL;
	while (1) {
		TekSynNode* entry = NULL;
		TekSynNode* ident = NULL;
		switch (token) {
			case TekToken_directive_import:
				entry = TekGenSyn_gen_import(w);
				break;
			case TekToken_end_of_file:
				goto END;
			default: {
				//
				// we must have a declaration, lets process the left hand side (identifier) first
				TekSynNode* ident = TekGenSyn_gen_expr_multi(w, tek_false);
				tek_ensure(ident);

				//
				// we must have a colon follow the left hand side.
				token = TekGenSyn_token_peek(w);
				TekGenSyn_ensure_token(w, token, ':', TekErrorKind_gen_syn_decl_mod_colon_must_follow_ident);

				//
				// allocate a declaration and link the left hand side.
				entry = TekGenSyn_alloc_node(w, TekSynNodeKind_decl, w->gen_syn.token_idx);
				entry->decl.ident_rel_idx = tek_rel_idx_s16(TekSynNode, ident, entry);

				//
				// process the right hand side (item)
				token = TekGenSyn_token_move_next(w);
				uint32_t item_token_idx = w->gen_syn.token_idx;
				TekGenSyn_token_move_next(w);
				TekSynNode* item = NULL;
				switch (token) {
					case TekToken_mod: item = TekGenSyn_gen_mod(w, item_token_idx, tek_false); break;
					case TekToken_proc: item = TekGenSyn_gen_proc(w, item_token_idx); break;
					case TekToken_var: item = TekGenSyn_gen_var(w, item_token_idx, tek_true); break;
					default:
						w->gen_syn.token_idx = item_token_idx;
						TekGenSyn_error_token(w, TekErrorKind_gen_syn_decl_expected_keyword);
						return NULL;
				}
				tek_ensure(item);

				entry->decl.item_rel_idx = tek_rel_idx_u16(TekSynNode, item, entry);
			};
		}
		tek_ensure(entry);

		//
		// link the entry to the previous entry
		if (prev_entry) {
			prev_entry->next_rel_idx = tek_rel_idx_u(TekSynNode, 24, entry, prev_entry);
		} else {
			first_entry = entry;
		}
		prev_entry = entry;

		token = TekGenSyn_token_peek(w);
		TekGenSyn_ensure_token(w, token, '\n', TekErrorKind_gen_syn_entry_expected_to_end_with_a_new_line);
		token = TekGenSyn_token_move_next(w);
	}
END:
	if (first_entry) {
		node->mod.entries_list_head_rel_idx = tek_rel_idx_u16(TekSynNode, first_entry, node);
	}
	return node;
}

TekSynNode* TekGenSyn_gen_var(TekWorker* w, uint32_t token_idx, TekBool is_global) {
	TekSynNode* node = TekGenSyn_gen_var_stub(w, token_idx, is_global);
	tek_ensure(node);

	//
	// process the initializer expressions if we have an assign symbol
	TekToken token = TekGenSyn_token_peek(w);
	if (token == '=') {
		TekGenSyn_token_move_next(w);

		TekSynNode* init_exprs = TekGenSyn_gen_expr_multi(w, tek_false);
		tek_ensure(init_exprs);

		node->var.init_exprs_rel_idx = tek_rel_idx_u8(TekSynNode, init_exprs, node);
	}

	return node;
}

TekSynNode* TekGenSyn_gen_var_stub(TekWorker* w, uint32_t token_idx, TekBool is_global) {
	TekSynNode* node = TekGenSyn_alloc_node(w, TekSynNodeKind_var, token_idx);

	//
	// generate the types if this is not the end of the var and we do not have an assign symbol.
	TekToken token = TekGenSyn_token_peek(w);
	if (token != '=' && token != '\n') {
		TekSynNode* types = TekGenSyn_gen_expr_multi(w, tek_false);
		tek_ensure(types);

		node->var.types_rel_idx = tek_rel_idx_u16(TekSynNode, types, node);
		token = TekGenSyn_token_peek(w);
	}

	//
	// process the directives
	TekVarFlags flags = is_global ? TekVarFlags_global : 0;
	while (1) {
		switch (token) {
			case TekToken_directive_static: flags |= TekVarFlags_static; break;
			case TekToken_directive_intrinsic: flags |= TekVarFlags_intrinsic; break;
			default: goto EXIT_FLAGS;
		}
		token = TekGenSyn_token_move_next(w);
	}

EXIT_FLAGS: {}
	node->var.flags = flags;
	return node;
}

TekSynNode* TekGenSyn_gen_import(TekWorker* w) {
	TekToken token = TekGenSyn_token_peek(w);

	TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_import, w->gen_syn.token_idx);
	TekSynNode* node = NULL;
	token = TekGenSyn_token_move_next(w);
	if (token == TekToken_lit_string) {
		node = TekGenSyn_alloc_node(w, TekSynNodeKind_import_file, w->gen_syn.token_idx);

		//
		// get the file path from the string table
		TekStrId path_str_id = TekGenSyn_token_value_take(w)->str_id;
		char* path = TekStrEntry_value(TekCompiler_strtab_get_entry(w->c, path_str_id));

		//
		// get an existing file with the same path or create a new one.
		// if the path is new, then a job is queued to get the file processed.
		TekFileId file_id = TekCompiler_file_get_or_create(w->c, path, w->gen_syn.file_id);
		tek_ensure(file_id);
		node->import_file.file_id = file_id;

		//
		// if an as keyword follows, then process the identifier that comes after.
		token = TekGenSyn_token_move_next(w);
		if (token == TekToken_as) {
			token = TekGenSyn_token_move_next(w);

			TekSynNode* ident = TekGenSyn_gen_expr(w);
			tek_ensure(ident);
			node->import_file.ident_rel_idx = tek_rel_idx_u16(TekSynNode, ident, node);

			TekGenSyn_token_move_next(w);
			return stmt;
		}
	} else {
		token = TekGenSyn_token_move_next(w);

		//
		// generate the path expression for the import
		node = TekGenSyn_gen_expr(w);
		tek_ensure(node);
	}
	stmt->import.expr_rel_idx = tek_rel_idx_u16(TekSynNode, node, stmt);

	return stmt;
}

TekSynNode* TekGenSyn_gen_proc(TekWorker* w, uint32_t token_idx) {
	TekSynNode* node = TekGenSyn_gen_proc_stub(w, token_idx, tek_false);
	tek_ensure(node);

	//
	// move to the open curly brace if there is only new lines in the way
	TekToken token = TekGenSyn_token_peek(w);
	if (token == '\n' && TekGenSyn_token_peek_ahead(w, 1) == '{') {
		token = TekGenSyn_token_move_next(w);
	}

	//
	// generate the statement block if we have an open curly brace
	if (token == '{') {
		TekSynNode* block = TekGenSyn_gen_stmt_block(w);
		tek_ensure(block);
		node->proc.stmt_block_rel_idx = tek_rel_idx_u16(TekSynNode, block, node);
	}

	return node;
}

TekSynNode* TekGenSyn_gen_proc_stub(TekWorker* w, uint32_t token_idx, TekBool is_type) {
	TekToken token = TekGenSyn_token_peek(w);

	TekSynNode* node = TekGenSyn_alloc_node(w, is_type ? TekSynNodeKind_type_proc : TekSynNodeKind_proc, token_idx);

	TekGenSyn_skip_new_lines(w, token);

	TekGenSyn_ensure_token(w, token, '(', TekErrorKind_gen_syn_proc_expected_parentheses);
	{
		//
		// generate the parameters
		TekSynNode* params_list_head = TekGenSyn_gen_proc_params(w, tek_false);
		tek_ensure(params_list_head);

		//
		// store them in the procedure node if we have any
		if (params_list_head != (TekSynNode*)0x1) {
			node->proc.params_list_head_rel_idx = tek_rel_idx_u8(TekSynNode, params_list_head, node);
		}

		token = TekGenSyn_token_peek(w);
	}
	TekGenSyn_skip_new_lines(w, token);

	if (token == TekToken_right_arrow) {
		//
		// we have return parameters
		token = TekGenSyn_token_move_next(w);
		TekGenSyn_ensure_token(w, token, '(', TekErrorKind_gen_syn_proc_expected_parentheses_to_follow_arrow);

		//
		// generate the return parameters
		TekSynNode* return_params_list_head = TekGenSyn_gen_proc_params(w, tek_true);
		tek_ensure(return_params_list_head);

		//
		// store them in the procedure node if we have any
		if (return_params_list_head != (TekSynNode*)0x1) {
			node->proc.return_params_list_head_rel_idx = tek_rel_idx_u8(TekSynNode, return_params_list_head, node);
		}

		token = TekGenSyn_token_peek(w);
	}
	TekGenSyn_skip_new_lines(w, token);

	TekProcCallConv call_conv = TekProcCallConv_tek;
	//
	// TODO: add the ability to set the calling convention of a procedure

	TekProcFlags flags = 0;
	while (1) {
		switch (token) {
			case TekToken_directive_noreturn: flags |= TekProcFlags_noreturn; break;
			case TekToken_directive_intrinsic: flags |= TekProcFlags_intrinsic; break;
			default: goto EXIT_FLAGS;
		}
		token = TekGenSyn_token_move_next(w);
	}
EXIT_FLAGS: {}
	node->proc.flags = flags;
	node->proc.call_conv = call_conv;
	return node;
}

TekSynNode* TekGenSyn_gen_proc_params(TekWorker* w, TekBool is_return_params) {
	TekSynNode* first_param = NULL;
	TekSynNode* prev_param = NULL;

	TekToken token = TekGenSyn_token_peek(w);
	TekGenSyn_assert_token(w, token, '(');
	token = TekGenSyn_token_move_next(w);

	while (token != ')') {
		TekGenSyn_skip_new_lines(w, token);

		TekSynNode* param = TekGenSyn_alloc_node(w, TekSynNodeKind_proc_param, w->gen_syn.token_idx);

		//
		// check to see if the parameter allows for variable arguments
		TekBool is_vararg = token == TekToken_ellipsis;
		param->proc_param.is_vararg = is_vararg;
		if (is_vararg) {
			if (is_return_params) {
				TekGenSyn_error_token(w, TekErrorKind_gen_syn_proc_params_cannot_have_vararg_in_return_params);
			}
			token = TekGenSyn_token_move_next(w);
		}

		//
		// generate the identifier, if we have a colon then process the type
		TekSynNode* ident = TekGenSyn_gen_expr(w);
		TekSynNode* type = NULL;
		token = TekGenSyn_token_peek(w);
		if (token == ':') {
			type = TekGenSyn_gen_type(w, tek_true);
			param->proc_param.ident_rel_idx = tek_rel_idx_u8(TekSynNode, ident, param);
		} else {
			// we have no colon so the identifier must be the type
			type = ident;
		}
		param->proc_param.type_rel_idx = tek_rel_idx_u16(TekSynNode, type, param);

		//
		// generate a default value node if we have one
		token = TekGenSyn_token_peek(w);
		if (token == '=') {
			token = TekGenSyn_token_move_next(w);

			TekSynNode* default_value_expr = TekGenSyn_gen_expr(w);
			tek_ensure(default_value_expr);
			param->proc_param.default_value_expr_rel_idx = tek_rel_idx_u8(TekSynNode, default_value_expr, param);

			token = TekGenSyn_token_peek(w);
		}

		//
		// add this parameter to the link list chain
		if (prev_param) {
			prev_param->next_rel_idx = tek_rel_idx_u(TekSynNode, 24, param, prev_param);
		} else {
			first_param = param;
		}
		prev_param = param;

		//
		// end if we have a close parentheses
		switch (token) {
			case ',':
			case '\n':
				break;
			case ')':
				goto PARAMS_END;
			default:
				TekGenSyn_error_token(w, TekErrorKind_gen_syn_proc_params_unexpected_delimiter);
				return NULL;
		}

		token = TekGenSyn_token_move_next(w);
	}

PARAMS_END:
	token = TekGenSyn_token_move_next(w);
	return first_param ? first_param : (TekSynNode*)0x1;
}

TekSynNode* TekGenSyn_gen_type(TekWorker* w, TekBool is_required) {
	TekToken token = TekGenSyn_token_peek(w);
	TekSynNode* type_qual = NULL;
	while (1) {
		switch (token) {
			case TekToken_mut:
			case TekToken_directive_noalias:
			case TekToken_directive_volatile: {
				type_qual = TekGenSyn_alloc_node(w, TekSynNodeKind_type_qualifier, w->gen_syn.token_idx);
				switch (token) {
					case TekToken_mut: type_qual->type_qual.flags = TekTypeQualifierFlags_mut; break;
					case TekToken_directive_noalias: type_qual->type_qual.flags = TekTypeQualifierFlags_noalias; break;
					case TekToken_directive_volatile: type_qual->type_qual.flags = TekTypeQualifierFlags_volatile; break;
					default: goto TYPE_QUAL_END;
				}

				token = TekGenSyn_token_move_next(w);
				break;
			};
			default: goto TYPE_QUAL_END;
		}
	}
TYPE_QUAL_END: {}

	TekSynNode* type = NULL;
	TekSynNodeKind kind;
	switch (token) {
		case TekToken_double_full_stop: {
			type = TekGenSyn_alloc_node(w, TekSynNodeKind_type_range, w->gen_syn.token_idx);
			token = TekGenSyn_token_move_next(w);
			// fallthrough
		};
		case TekToken_ident:
		case TekToken_ident_abstract:
		case '.':
		case '\\':
		case '~':
			type = TekGenSyn_gen_expr(w);
			break;
		case TekToken_proc:
			type = TekGenSyn_gen_proc_stub(w, w->gen_syn.token_idx, tek_true);
			break;
		case '&':
			kind = TekSynNodeKind_type_ref;
			goto PTR;
		case '*':
			kind = TekSynNodeKind_type_ptr;
PTR:
			type = TekGenSyn_gen_type_ptr(w, kind);
			break;
		case '[':
			type = TekGenSyn_gen_type_array(w);
			break;
		case 'U':
		case 'S':
			type = TekGenSyn_gen_type_bounded_int(w, token == 'S');
			break;
		default:
			if (is_required) {
				TekGenSyn_error_token(w, TekErrorKind_gen_syn_type_unexpected_token);
				return NULL;
			} else {
				type = TekGenSyn_alloc_node(w, TekSynNodeKind_type_implicit, w->gen_syn.token_idx);
			}
			break;
	}

	//
	// if we have one, the type qualifier will come directly before the type.
	return type_qual ? type_qual : type;
}

TekSynNode* TekGenSyn_gen_type_ptr(TekWorker* w, TekSynNodeKind kind) {
	TekSynNode* type = TekGenSyn_alloc_node(w, kind, w->gen_syn.token_idx);

	TekToken token = TekGenSyn_token_move_next(w);

	//
	// if this is a relative pointer, then generate the relative type
	if (token == '~') {
		token = TekGenSyn_token_move_next(w);
		TekSynNode* rel_type = TekGenSyn_gen_type(w, tek_true);
		tek_ensure(rel_type);
		type->type_ptr.rel_type_rel_idx = tek_rel_idx_u8(TekSynNode, rel_type, type);
		token = TekGenSyn_token_peek(w);
	}

	//
	// generate the pointer's element type
	TekSynNode* elmt_type = TekGenSyn_gen_type(w, tek_true);
	tek_ensure(elmt_type);
	type->type_ptr.elmt_type_rel_idx = tek_rel_idx_u8(TekSynNode, elmt_type, type);
	return type;
}

TekSynNode* TekGenSyn_gen_type_array(TekWorker* w) {
	//
	// if nothing is in the square brackets then it is a view type
	if (TekGenSyn_token_peek_ahead(w, 1) == ']') {
		TekGenSyn_token_move_ahead(w, 2);
		return TekGenSyn_gen_type_ptr(w, TekSynNodeKind_type_view);
	}

	//
	// we have a stack type if there is a double full stop after the open square bracket
	TekSynNodeKind kind = TekSynNodeKind_type_array;
	TekToken token = TekGenSyn_token_move_next(w);
	if (token == TekToken_double_full_stop) {
		kind = TekSynNodeKind_type_stack;
		token = TekGenSyn_token_move_next(w);
	}

	TekSynNode* type = TekGenSyn_alloc_node(w, kind, w->gen_syn.token_idx);

	// gen count expression that is known to sit directly after our arr type expression in memory.
	tek_ensure(TekGenSyn_gen_expr(w));

	token = TekGenSyn_token_peek(w);
	TekGenSyn_ensure_token(w, token, ']', TekErrorKind_gen_syn_type_array_expected_close_bracket);
	token = TekGenSyn_token_move_next(w);

	//
	// generate the array's element type
	TekSynNode* elmt_type = TekGenSyn_gen_type(w, tek_true);
	tek_ensure(elmt_type);
	type->type_array.elmt_type_rel_idx = tek_rel_idx_u16(TekSynNode, elmt_type, type);

	return type;
}

TekSynNode* TekGenSyn_gen_type_bounded_int(TekWorker* w, TekBool is_signed) {
	TekSynNode* type = TekGenSyn_alloc_node(w, TekSynNodeKind_type_bounded_int, w->gen_syn.token_idx);
	type->type_bounded_int.is_signed = is_signed;

	TekToken token = TekGenSyn_token_move_next(w);
	TekGenSyn_ensure_token(w, token, '|', TekErrorKind_gen_syn_type_bounded_int_expected_pipe_and_bit_count);
	token = TekGenSyn_token_move_next(w);

	// the bit count expression is directly after the type's TekSynNode in memory.
	// so no need to store this anywhere
	tek_ensure(TekGenSyn_gen_expr(w));

	token = TekGenSyn_token_peek(w);
	if (token == '|') {
		TekSynNode* range_expr = TekGenSyn_gen_expr(w);
		tek_ensure(range_expr);
		type->type_bounded_int.range_expr_rel_idx = tek_rel_idx_u16(TekSynNode, range_expr, type);
	}

	return type;
}

TekSynNode* TekGenSyn_gen_expr_multi(TekWorker* w, TekBool process_named_args) {
	TekSynNode* first_expr = NULL;
	TekSynNode* prev_expr = NULL;
	uint32_t count = 0;
	uint32_t token_idx = w->gen_syn.token_idx;
	while (1) {
		count += 1;

		//
		// generate the expression
		TekSynNode* expr = TekGenSyn_gen_expr(w);
		tek_ensure(expr);

		TekToken token = TekGenSyn_token_peek(w);
		if (process_named_args && token == ':') {
			//
			// we found a colon after the expression, so wrap this in a named argument expression.
			// put the previously generated expression as the identifier of this named argument.
			TekSynNode* named_arg_expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_named_arg, w->gen_syn.token_idx);
			named_arg_expr->expr_named_arg.ident_rel_idx = tek_rel_idx_s16(TekSynNode, expr, named_arg_expr);

			//
			// the value expression follows the named argument expression directly after in memory.
			// so there is no need to link this in the structure.
			token = TekGenSyn_token_move_next(w);
			tek_ensure(TekGenSyn_gen_expr(w));

			//
			// make the named argument the expression.
			expr = named_arg_expr;
			token = TekGenSyn_token_peek(w);
		}

		//
		// if done have a comma, then this is the end of the multi expression.
		// if there is only a single expression, just return that.
		if (token != ',') {
			if (first_expr) {
				prev_expr->next_rel_idx = tek_rel_idx_u(TekSynNode, 24, expr, prev_expr);
				break;
			}
			else return expr;
		}
		TekGenSyn_token_move_next(w);
		TekGenSyn_skip_new_lines(w, token);

		//
		// add this expression to the linked list chain
		if (prev_expr) {
			prev_expr->next_rel_idx = tek_rel_idx_u(TekSynNode, 24, expr, prev_expr);
		} else {
			first_expr = expr;
		}
		prev_expr = expr;
	}

	//
	// create a wrapper node to hold the multiple expression and a count.
	TekSynNode* multi_expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_multi, token_idx);
	multi_expr->expr_multi.list_head_rel_idx = tek_rel_idx_s16(TekSynNode, first_expr, multi_expr);
	multi_expr->expr_multi.count = count;

	return multi_expr;
}

TekSynNode* TekGenSyn_gen_expr(TekWorker* w) {
	TekToken token = TekGenSyn_token_peek(w);

	//
	// if the expression starts with a full stop.
	// then this should be an absolute path.
	// return the expr_root_mod as we can get to the other expression that sit directly after in memory.
	TekSynNode* expr_root_mod = NULL;
	if (token == '.') {
		expr_root_mod = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_root_mod, w->gen_syn.token_idx);
	}

	TekSynNode* expr = TekGenSyn_gen_expr_with(w, 0);
	return expr_root_mod ? expr_root_mod : expr;
}

TekSynNode* TekGenSyn_gen_expr_with(TekWorker* w, uint8_t min_precedence) {
	//
	// generate the left hand side expression
	TekSynNode* left_expr = TekGenSyn_gen_expr_unary(w);
	tek_ensure(left_expr);

	//
	// loop until a we do not find a binary operator, or the operator is less or equal
	// importance than our caller's binary expression.
	while (1) {
		//
		// get the current token and get it's precedence if it is a binary operator symbol.
		// the lower the precedence, the more important it is.
		uint8_t precedence;
		TekToken token = TekGenSyn_token_peek(w);
		TekBinaryOp binary_op = TekGenSyn_token_to_bin_op(token, &precedence);
		// stop the loop if this is not a binary operator or it is less or equal important than the caller's.
		if ((min_precedence > 0 && precedence >= min_precedence) || binary_op == TekBinaryOp_none) break;

		uint32_t binary_op_token_idx = w->gen_syn.token_idx;
		token = TekGenSyn_token_move_next(w);

		//
		// generate the right hand ride expression of the binary operation.
		TekSynNode* right_expr = NULL;
		if (binary_op == TekBinaryOp_call) {
			// call expressions are multiple expressions that lets generate that.
			if (TekGenSyn_token_peek(w) == ')') {
				// no arguments for the call expression.
				right_expr = (TekSynNode*)0x1;
			} else {
				right_expr = TekGenSyn_gen_expr_multi(w, tek_true);
				token = TekGenSyn_token_peek(w);
				TekGenSyn_ensure_token(w, token, ')', TekErrorKind_gen_syn_expr_call_expected_close_parentheses);
			}
			token = TekGenSyn_token_move_next(w);
		} else {
			//
			// recursively call with the precedence of this binary operation.
			right_expr = TekGenSyn_gen_expr_with(w, precedence);
			if (binary_op == TekBinaryOp_index) {
				token = TekGenSyn_token_peek(w);
				TekGenSyn_ensure_token(w, token, ']', TekErrorKind_gen_syn_expr_index_expected_close_bracket);
				TekGenSyn_token_move_next(w);
			}
		}
		tek_ensure(right_expr);

		TekSynNode* expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_op_binary, binary_op_token_idx);
		expr->binary.op = binary_op;
		expr->binary.left_rel_idx = tek_rel_idx_s16(TekSynNode, left_expr, expr);

		// call expression might not have any arguments, so check for that
		// before setting the right hand side.
		if (right_expr != (TekSynNode*)0x1) {
			expr->binary.right_rel_idx = tek_rel_idx_s16(TekSynNode, right_expr, expr);
		}

		//
		// make this binary expression we just made, the next left hand side expression.
		left_expr = expr;
	}

	return left_expr;
}

TekSynNode* TekGenSyn_gen_expr_unary(TekWorker* w) {
	TekToken token = TekGenSyn_token_peek(w);
	TekSynNodeKind kind;
	TekUnaryOp unary_op = 0;
	uint8_t up_parent_mods_count = 0;
	switch (token) {
		case TekToken_ident: kind = TekSynNodeKind_ident; goto IDENT;
		case TekToken_ident_abstract: kind = TekSynNodeKind_ident_abstract; goto IDENT;
IDENT:
		{
			TekSynNode* expr = TekGenSyn_alloc_node(w, kind, w->gen_syn.token_idx);
			expr->ident_str_id = TekGenSyn_token_value_take(w)->str_id;
			TekGenSyn_token_move_next(w);
			return expr;
		};
		case TekToken_label: kind = TekSynNodeKind_label; goto VALUE;
		case TekToken_lit_uint: kind = TekSynNodeKind_expr_lit_uint; goto VALUE;
		case TekToken_lit_sint: kind = TekSynNodeKind_expr_lit_sint; goto VALUE;
		case TekToken_lit_float: kind = TekSynNodeKind_expr_lit_float; goto VALUE;
		case TekToken_lit_bool: kind = TekSynNodeKind_expr_lit_bool; goto VALUE;
		case TekToken_lit_string: kind = TekSynNodeKind_expr_lit_string; goto VALUE;
VALUE:
		{
			TekSynNode* expr = TekGenSyn_alloc_node(w, kind, w->gen_syn.token_idx);
			expr->token_value_idx = w->gen_syn.token_value_idx;
			w->gen_syn.token_value_idx += 1;
			TekGenSyn_token_move_next(w);
			return expr;
		};

		case '\\': {
			uint32_t token_idx = w->gen_syn.token_idx;
			up_parent_mods_count = 0;
			while (TekGenSyn_token_move_next(w) == '\\') {
				up_parent_mods_count += 1;
			}

			TekSynNode* expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_up_parent_mods, token_idx);
			expr->up_parent_mods_count = up_parent_mods_count;

			// the child of this up parent mods expression will come directly after in memory.
			// so there is no need to store it anywhere.
			tek_ensure(TekGenSyn_gen_expr_unary(w));

			return expr;
		};

		case '!': unary_op = TekUnaryOp_logical_not; goto UNARY;
		case '~': unary_op = TekUnaryOp_bit_not; goto UNARY;
		case '-': unary_op = TekUnaryOp_negate; goto UNARY;
		case '&': unary_op = TekUnaryOp_address_of; goto UNARY;
		case '*': unary_op = TekUnaryOp_dereference; goto UNARY;
		case '?': unary_op = TekUnaryOp_ensure_value; goto UNARY;
		case TekToken_question_and_exclamation_mark: unary_op = TekUnaryOp_ensure_null; goto UNARY;
UNARY:
		{
			TekSynNode* expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_op_unary, w->gen_syn.token_idx);
			expr->unary.op = unary_op;
			TekGenSyn_token_move_next(w);
			return expr;
		};
		case '(': {
			token = TekGenSyn_token_move_next(w);

			TekSynNode* expr = TekGenSyn_gen_expr(w);
			tek_ensure(expr);

			token = TekGenSyn_token_peek(w);
			// the lexer should ensure the correct close bracket.
			TekGenSyn_ensure_token(w, token, ')', TekErrorKind_gen_syn_expr_expected_close_parentheses);
			TekGenSyn_token_move_next(w);

			return expr;
		};
		case '{':
			kind = TekSynNodeKind_expr_stmt_block;
			goto STMT_BLOCK;
		case TekToken_loop:
			kind = TekSynNodeKind_expr_loop;
			token = TekGenSyn_token_move_next(w);
			TekGenSyn_ensure_token(w, token, '{', TekErrorKind_gen_syn_expr_loop_expected_curly_brace);
			goto STMT_BLOCK;
STMT_BLOCK:
			return TekGenSyn_gen_stmt_block_with(w, kind);

		case TekToken_if:
			return TekGenSyn_gen_expr_if(w);

		case TekToken_match:
			return TekGenSyn_gen_expr_match(w);

		case TekToken_for:
			return TekGenSyn_gen_for_expr(w);

		case '[': {
			TekSynNode* expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_lit_array, w->gen_syn.token_idx);
			TekGenSyn_token_move_next(w);

			// these expressions start directly after our array expression in memory.
			// so there is not need to store the returned pointer anywhere.
			tek_ensure(TekGenSyn_gen_expr_multi(w, tek_true));

			token = TekGenSyn_token_peek(w);
			TekGenSyn_ensure_token(w, token, ']', TekErrorKind_gen_syn_expr_array_expected_close_bracket);
			TekGenSyn_token_move_next(w);
			return expr;
		};

		case TekToken_directive_type:
			TekGenSyn_token_move_next(w);
			// fallthrough
		case TekToken_union:
		case TekToken_proc:
		case TekToken_mut:
		case TekToken_directive_noalias:
		case TekToken_directive_volatile:
		case 'U':
		case 'S':
TYPE_REF:
			return TekGenSyn_gen_type(w, tek_true);

		case TekToken_ellipsis: {
			TekSynNode* node = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_vararg_spread, w->gen_syn.token_idx);
			TekGenSyn_token_move_next(w);
			tek_ensure(TekGenSyn_gen_expr_unary(w));
			return node;
		};
	}

	char token_name[128];
	TekToken_as_string(token, token_name, sizeof(token_name));
	tek_abort("unreachable code -> token '%s' has not been handled when generating a unary expression", token_name);
}

TekSynNode* TekGenSyn_gen_expr_if(TekWorker* w) {
	TekToken token = TekGenSyn_token_peek(w);
	TekGenSyn_assert_tokens(w, token, TekToken_if, TekToken_compile_time_if);

	TekSynNode* expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_if, w->gen_syn.token_idx);
	TekGenSyn_token_move_next(w);

	//
	// generate condition expression that is known to sit directly after our if expression in memory.
	tek_ensure(TekGenSyn_gen_expr(w));

	//
	// generate the true block
	TekSynNode* success_expr = TekGenSyn_gen_stmt_block(w);
	tek_ensure(success_expr);
	expr->expr_if.success_rel_idx = tek_rel_idx_u16(TekSynNode, success_expr, expr);

	//
	// generate the false block if we have an else keyword
	token = TekGenSyn_token_peek(w);
	if (token == TekToken_else) {
		TekSynNode* else_expr = NULL;
		token = TekGenSyn_token_move_next(w);
		switch (token) {
			case '{': else_expr = TekGenSyn_gen_stmt_block(w); break;
			case TekToken_if: else_expr = TekGenSyn_gen_expr_if(w); break;
			default:
				TekGenSyn_error_token(w, TekErrorKind_gen_syn_expr_if_else_unexpected_token);
				return NULL;
		}
		tek_ensure(else_expr);
		expr->expr_if.else_rel_idx = tek_rel_idx_u16(TekSynNode, else_expr, expr);
	}
	return expr;
}

TekSynNode* TekGenSyn_gen_expr_match(TekWorker* w) {
	TekToken token = TekGenSyn_token_peek(w);
	TekGenSyn_assert_tokens(w, token, TekToken_match, TekToken_compile_time_match);

	TekSynNode* expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_match, w->gen_syn.token_idx);
	token = TekGenSyn_token_move_next(w);

	//
	// generate condition expression that is known to sit directly after our match expression in memory.
	tek_ensure(TekGenSyn_gen_expr_multi(w, tek_false));

	token = TekGenSyn_token_peek(w);
	TekGenSyn_ensure_token(w, token, '{', TekErrorKind_gen_syn_expr_match_must_define_cases);

	TekSynNode* first_case = NULL;
	TekSynNode* prev_case = NULL;
	while (token != '}') {
		token = TekGenSyn_token_move_next(w);
		TekGenSyn_skip_new_lines(w, token);

		//
		// we either have a case, else or an statement block for cases.
		TekSynNode* case_expr = NULL;
		switch (token) {
			case TekToken_case:
				case_expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_match_case, w->gen_syn.token_idx);
				TekGenSyn_token_move_next(w);
				// the case condition will follow directly follow the case in memory.
				// so no need to store it anywhere
				tek_ensure(TekGenSyn_gen_expr_multi(w, tek_false));
				break;

			case TekToken_else:
				case_expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_match_else, w->gen_syn.token_idx);
				break;

			case '{':
				case_expr = TekGenSyn_gen_stmt_block(w);
				break;
			case '}':
				goto END;
			default:
				TekGenSyn_error_token(w, TekErrorKind_gen_syn_expr_match_unexpected_token);
				return NULL;
		}

		//
		// add the case to the linked list chain
		if (prev_case) {
			prev_case->next_rel_idx = tek_rel_idx_u(TekSynNode, 24, prev_case, case_expr);
		} else {
			first_case = case_expr;
		}
		prev_case = case_expr;
	}

END:
	if (first_case)
		expr->expr_match.cases_list_head_rel_idx = tek_rel_idx_u16(TekSynNode, first_case, expr);

	TekGenSyn_token_move_next(w);
	return expr;
}

TekSynNode* TekGenSyn_gen_for_expr(TekWorker* w) {
	TekSynNode* expr = TekGenSyn_alloc_node(w, TekSynNodeKind_expr_for, w->gen_syn.token_idx);
	TekToken token = TekGenSyn_token_move_next(w);

	//
	// generate the identifier expressions that is known to sit directly after our for statement in memory.
	tek_ensure(TekGenSyn_gen_expr_multi(w, tek_false));

	//
	// generate the types if we have any that are explicitly defined
	token = TekGenSyn_token_peek(w);
	if (token == ':') {
		TekGenSyn_token_move_next(w);

		TekSynNode* types_list_head = TekGenSyn_gen_expr_multi(w, tek_false);
		tek_ensure(types_list_head);
		expr->expr_for.types_list_head_rel_idx = tek_rel_idx_u16(TekSynNode, types_list_head, expr);

		token = TekGenSyn_token_peek(w);
	}

	//
	// make sure we have the 'in' keyword
	TekGenSyn_ensure_token(w, token, TekToken_in, TekErrorKind_gen_syn_expr_for_expected_in_keyword);
	token = TekGenSyn_token_move_next(w);

	//
	// check for the is by value and is reverse symbols
	TekBool is_by_value, is_reverse;
	{
		is_by_value = token == '*';
		if (is_by_value) {
			token = TekGenSyn_token_move_next(w);
		}

		is_reverse = token == '!';
		if (is_reverse) {
			token = TekGenSyn_token_move_next(w);

			// check this one again, just in case they are the other way round
			is_by_value = token == '*';
			if (is_by_value) {
				token = TekGenSyn_token_move_next(w);
			}
		}
	}
	expr->expr_for.is_by_value = is_by_value;
	expr->expr_for.is_reverse = is_reverse;

	//
	// generate the iterator expression
	TekSynNode* iter_expr = TekGenSyn_gen_expr(w);
	tek_ensure(iter_expr);
	expr->expr_for.iter_expr_rel_idx = tek_rel_idx_u16(TekSynNode, iter_expr, expr);

	//
	// generate the statement block
	TekSynNode* stmt_block_expr = TekGenSyn_gen_stmt_block(w);
	tek_ensure(stmt_block_expr);
	expr->expr_for.stmt_block_expr_rel_idx = tek_rel_idx_u16(TekSynNode, stmt_block_expr, expr);

	return expr;
}

TekSynNode* TekGenSyn_gen_stmt_block(TekWorker* w) {
	return TekGenSyn_gen_stmt_block_with(w, TekSynNodeKind_expr_stmt_block);
}

TekSynNode* TekGenSyn_gen_stmt_block_with(TekWorker* w, TekSynNodeKind kind) {
	TekSynNode* expr = TekGenSyn_alloc_node(w, kind, w->gen_syn.token_idx);

	//
	// move off the curly brace
	TekToken token = TekGenSyn_token_peek(w);
	TekGenSyn_assert_token(w, token, '{');
	token = TekGenSyn_token_move_next(w);

	TekSynNode* first_stmt = NULL;
	TekSynNode* prev_stmt = NULL;
	while (1) {
		TekGenSyn_skip_new_lines(w, token);
		if (token == '}') break;

		//
		// generate the statement
		TekSynNode* stmt = TekGenSyn_gen_stmt(w);
		tek_ensure(stmt);

		//
		// add the statement to the previous statement in the linked list
		if (prev_stmt) {
			prev_stmt->next_rel_idx = tek_rel_idx_u(TekSynNode, 24, stmt, prev_stmt);
		} else {
			first_stmt = stmt;
		}
		prev_stmt = stmt;

		token = TekGenSyn_token_peek(w);
	}

	expr->expr_stmt_block.stmts_list_head_rel_idx = tek_rel_idx_u16(TekSynNode, first_stmt, expr);
	TekGenSyn_token_move_next(w);
	return expr;
}

TekSynNode* TekGenSyn_gen_stmt(TekWorker* w) {
	TekToken token = TekGenSyn_token_peek(w);
	switch (token) {
		case TekToken_return: {
			TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_stmt_return, w->gen_syn.token_idx);
			token = TekGenSyn_token_move_next(w);

			//
			// store the label if we have one
			if (token == TekToken_label) {
				stmt->stmt_return.label_str_id = TekGenSyn_token_value_take(w)->str_id;
				token = TekGenSyn_token_move_next(w);
			}

			//
			// generate the return arguments if we have any
			if (token != '\n') {
				TekSynNode* args_expr = TekGenSyn_gen_expr_multi(w, tek_true);
				tek_ensure(args_expr);
				if (args_expr != (TekSynNode*)0x1) {
					stmt->stmt_return.args_expr_rel_idx = tek_rel_idx_u16(TekSynNode, args_expr, stmt);
				}
			}
			return stmt;
		};
		case TekToken_continue: {
			TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_stmt_continue, w->gen_syn.token_idx);
			token = TekGenSyn_token_move_next(w);

			//
			// store the label if we have one
			if (token == TekToken_label) {
				stmt->stmt_continue.label_str_id = TekGenSyn_token_value_take(w)->str_id;
				TekGenSyn_token_move_next(w);
			}

			return stmt;
		};
		case TekToken_defer: {
			TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_stmt_defer, w->gen_syn.token_idx);
			token = TekGenSyn_token_move_next(w);

			//
			// generate a single expression or a statement block
			TekSynNode* expr = token == '{'
				? TekGenSyn_gen_stmt_block(w)
				: TekGenSyn_gen_expr(w);
			tek_ensure(expr);

			stmt->stmt_defer.expr_rel_idx = tek_rel_idx_u16(TekSynNode, expr, stmt);
			return stmt;
		};
		case TekToken_goto: {
			TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_stmt_goto, w->gen_syn.token_idx);
			TekGenSyn_token_move_next(w);

			// generate the expression that is known to sit directly after our goto statement in memory.
			tek_ensure(TekGenSyn_gen_expr(w));
			return stmt;
		};
		case TekToken_directive_fallthrough: {
			TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_stmt_fallthrough, w->gen_syn.token_idx);
			TekGenSyn_token_move_next(w);
			return stmt;
		};
		default: break;
	}

	//
	// generate an expression.
	// depending on the next token, this could also be the
	// left hand side of a binary statement/declaration.
	TekSynNode* expr = TekGenSyn_gen_expr_multi(w, tek_false);
	tek_ensure(expr);

	token = TekGenSyn_token_peek(w);
	TekBinaryOp binary_op = TekBinaryOp_none;
	switch (token) {
		case ':': {
			TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_decl, w->gen_syn.token_idx);

			//
			// the expression we just generated is used as the identifier for this declaration
			stmt->decl.ident_rel_idx = tek_rel_idx_s16(TekSynNode, expr, stmt);

			//
			// we only allow variable declarations in statement block scopes
			token = TekGenSyn_token_move_next(w);
			TekGenSyn_ensure_token(w, token, TekToken_var, TekErrorKind_gen_syn_stmt_only_allow_var_decl);

			uint32_t token_idx = w->gen_syn.token_idx;
			token = TekGenSyn_token_move_next(w);

			//
			// generate the variable
			TekSynNode* item = TekGenSyn_gen_var(w, token_idx, tek_false);
			tek_ensure(item);
			stmt->decl.item_rel_idx = tek_rel_idx_u16(TekSynNode, item, stmt);

			return stmt;
		};

		case '=':
			binary_op = TekBinaryOp_none;
			goto BINARY_OP;
		case TekToken_assign_add:
			binary_op = TekBinaryOp_add;
			goto BINARY_OP;
		case TekToken_assign_subtract:
			binary_op = TekBinaryOp_subtract;
			goto BINARY_OP;
		case TekToken_assign_multiply:
			binary_op = TekBinaryOp_multiply;
			goto BINARY_OP;
		case TekToken_assign_divide:
			binary_op = TekBinaryOp_divide;
			goto BINARY_OP;
		case TekToken_assign_remainder:
			binary_op = TekBinaryOp_remainder;
			goto BINARY_OP;
		case TekToken_assign_bit_and:
			binary_op = TekBinaryOp_bit_and;
			goto BINARY_OP;
		case TekToken_assign_bit_or:
			binary_op = TekBinaryOp_bit_or;
			goto BINARY_OP;
		case TekToken_assign_bit_xor:
			binary_op = TekBinaryOp_bit_xor;
			goto BINARY_OP;
		case TekToken_assign_bit_shift_left:
			binary_op = TekBinaryOp_bit_shift_left;
			goto BINARY_OP;
		case TekToken_assign_bit_shift_right:
			binary_op = TekBinaryOp_bit_shift_right;
			goto BINARY_OP;
BINARY_OP:
		{
			//
			// we have a assign statement here
			TekSynNode* stmt = TekGenSyn_alloc_node(w, TekSynNodeKind_stmt_assign, w->gen_syn.token_idx);
			stmt->binary.op = binary_op;

			//
			// link the left hand side expression we generated before this.
			// this is the assignee
			stmt->binary.left_rel_idx = tek_rel_idx_s16(TekSynNode, expr, stmt);
			token = TekGenSyn_token_move_next(w);

			//
			// now generate the right hand side expression.
			// this is the value being assigned
			TekSynNode* right_expr = TekGenSyn_gen_expr_multi(w, tek_false);
			tek_ensure(right_expr);
			stmt->binary.right_rel_idx = tek_rel_idx_s16(TekSynNode, right_expr, stmt);

			return stmt;
		};
		default: break;
	}

	//
	// return the expression as there was no statement here.
	return expr;
}

TekBinaryOp TekGenSyn_token_to_bin_op(TekToken token, uint8_t* precedence_out) {
    switch (token) {
		case '.':
			*precedence_out = 1;
			return TekBinaryOp_field_access;

		case '(':
			*precedence_out = 2;
			return TekBinaryOp_call;
		case '[':
			*precedence_out = 2;
			return TekBinaryOp_index;
		case TekToken_double_full_stop:
			*precedence_out = 2;
			return TekBinaryOp_range;
		case TekToken_double_full_stop_equal:
			*precedence_out = 2;
			return TekBinaryOp_range_inclusive;

		case TekToken_as:
			*precedence_out = 3;
			return TekBinaryOp_as;

		case '*':
			*precedence_out = 4;
			return TekBinaryOp_multiply;
		case '/':
			*precedence_out = 4;
			return TekBinaryOp_divide;
		case '%':
			*precedence_out = 4;
			return TekBinaryOp_remainder;

		case '+':
			*precedence_out = 5;
			return TekBinaryOp_add;
		case '-':
			*precedence_out = 5;
			return TekBinaryOp_subtract;

		case TekToken_concat:
			*precedence_out = 5;
			return TekBinaryOp_concat;

		case TekToken_assign_bit_shift_left:
			*precedence_out = 6;
			return TekBinaryOp_bit_shift_left;
		case TekToken_assign_bit_shift_right:
			*precedence_out = 6;
			return TekBinaryOp_bit_shift_right;

		case '&':
			*precedence_out = 7;
			return TekBinaryOp_bit_and;

		case '^':
			*precedence_out = 8;
			return TekBinaryOp_bit_xor;

		case '|':
			*precedence_out = 9;
			return TekBinaryOp_bit_or;

		case TekToken_double_equal:
			*precedence_out = 10;
			return TekBinaryOp_logical_equal;
		case TekToken_not_equal:
			*precedence_out = 10;
			return TekBinaryOp_logical_not_equal;
		case '<':
			*precedence_out = 10;
			return TekBinaryOp_logical_less_than;
		case TekToken_less_than_or_eq:
			*precedence_out = 10;
			return TekBinaryOp_logical_less_than_or_equal;
		case '>':
			*precedence_out = 10;
			return TekBinaryOp_logical_greater_than;
		case TekToken_greater_than_or_eq:
			*precedence_out = 10;
			return TekBinaryOp_logical_greater_than_or_equal;

		case TekToken_double_ampersand:
			*precedence_out = 11;
			return TekBinaryOp_logical_and;

		case TekToken_double_pipe:
			*precedence_out = 12;
			return TekBinaryOp_logical_or;
		default: break;
	}

	*precedence_out = 0;
	return TekBinaryOp_none;
}

