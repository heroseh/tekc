
char* TekProcCallConv_strings[TekProcCallConv_COUNT] = {
	[TekProcCallConv_tek] = "tek",
	[TekProcCallConv_c] = "c",
};

char* TekSynNodeKind_strings[] = {
	[TekSynNodeKind_ident] = "ident",
	[TekSynNodeKind_ident_abstract] = "ident_abstract",
	[TekSynNodeKind_anon_struct_ident] = "anon_struct_ident",
	[TekSynNodeKind_label] = "label",
	[TekSynNodeKind_decl] = "decl",
	[TekSynNodeKind_lib_ref] = "lib_ref",
	[TekSynNodeKind_lib_extern] = "lib_extern",
	[TekSynNodeKind_mod] = "mod",
	[TekSynNodeKind_proc] = "proc",
	[TekSynNodeKind_proc_param] = "proc_param",
	[TekSynNodeKind_macro] = "macro",
	[TekSynNodeKind_interf] = "interf",
	[TekSynNodeKind_var] = "var",
	[TekSynNodeKind_alias] = "alias",
	[TekSynNodeKind_import] = "import",
	[TekSynNodeKind_import_file] = "import_file",
	[TekSynNodeKind_type_struct] = "type_struct",
	[TekSynNodeKind_struct_field] = "struct_field",
	[TekSynNodeKind_type_enum] = "type_enum",
	[TekSynNodeKind_enum_field] = "enum_field",
	[TekSynNodeKind_type_proc] = "type_proc",
	[TekSynNodeKind_type_bounded_int] = "type_bounded_int",
	[TekSynNodeKind_type_ptr] = "type_ptr",
	[TekSynNodeKind_type_ref] = "type_ref",
	[TekSynNodeKind_type_view] = "type_view",
	[TekSynNodeKind_type_array] = "type_array",
	[TekSynNodeKind_type_stack] = "type_stack",
	[TekSynNodeKind_type_range] = "type_range",
	[TekSynNodeKind_type_implicit] = "type_implicit",
	[TekSynNodeKind_type_qualifier] = "type_qualifier",
	[TekSynNodeKind_stmt_list_header] = "stmt_list_header",
	[TekSynNodeKind_stmt_assign] = "stmt_assign",
	[TekSynNodeKind_stmt_return] = "stmt_return",
	[TekSynNodeKind_stmt_continue] = "stmt_continue",
	[TekSynNodeKind_stmt_goto] = "stmt_goto",
	[TekSynNodeKind_stmt_defer] = "stmt_defer",
	[TekSynNodeKind_stmt_fallthrough] = "stmt_fallthrough",
	[TekSynNodeKind_expr_list_header] = "expr_list_header",
	[TekSynNodeKind_expr_multi] = "expr_multi",
	[TekSynNodeKind_expr_op_binary] = "expr_op_binary",
	[TekSynNodeKind_expr_op_unary] = "expr_op_unary",
	[TekSynNodeKind_expr_if] = "expr_if",
	[TekSynNodeKind_expr_match] = "expr_match",
	[TekSynNodeKind_expr_match_case] = "expr_match_case",
	[TekSynNodeKind_expr_match_else] = "expr_match_else",
	[TekSynNodeKind_expr_for] = "expr_for",
	[TekSynNodeKind_expr_named_arg] = "expr_named_arg",
	[TekSynNodeKind_expr_compile_time] = "expr_compile_time",
	[TekSynNodeKind_expr_stmt_block] = "expr_stmt_block",
	[TekSynNodeKind_expr_loop] = "expr_loop",
	[TekSynNodeKind_expr_vararg_spread] = "expr_vararg_spread",
	[TekSynNodeKind_expr_lit_uint] = "expr_lit_uint",
	[TekSynNodeKind_expr_lit_sint] = "expr_lit_sint",
	[TekSynNodeKind_expr_lit_float] = "expr_lit_float",
	[TekSynNodeKind_expr_lit_bool] = "expr_lit_bool",
	[TekSynNodeKind_expr_lit_string] = "expr_lit_string",
	[TekSynNodeKind_expr_lit_array] = "expr_lit_array",
	[TekSynNodeKind_expr_up_parent_mods] = "expr_up_parent_mods",
	[TekSynNodeKind_expr_root_mod] = "expr_root_mod",
};

char* TekToken_strings_non_ascii[] = {
	[TekToken_ident] = "identifier",
	[TekToken_ident_abstract] = "abstract identifier",
	[TekToken_label] = "label",
    [TekToken_end_of_file] = "end_of_file",

    //
    // literals
    //
    [TekToken_lit_uint] = "literial uint",
    [TekToken_lit_sint] = "literial sint",
    [TekToken_lit_float] = "literial float",
    [TekToken_lit_bool] = "literial bool",
    [TekToken_lit_string] = "literial string",

	//
    // grouped symbols
	//
    [TekToken_assign_add] = "+=",
    [TekToken_assign_subtract] = "-=",
    [TekToken_assign_multiply] = "*=",
    [TekToken_assign_divide] = "/=",
    [TekToken_assign_remainder] = "%=",
    [TekToken_assign_bit_and] = "&=",
    [TekToken_assign_bit_or] = "|=",
    [TekToken_assign_bit_xor] = "^=",
    [TekToken_assign_bit_shift_left] = "<<=",
    [TekToken_assign_bit_shift_right] = ">>=",
    [TekToken_assign_concat] = "++=",
    [TekToken_concat] = "++",
    [TekToken_right_arrow] = "->",
    [TekToken_thick_right_arrow] = "=>",
    [TekToken_double_equal] = "==",
    [TekToken_not_equal] = "!=",
    [TekToken_less_than_or_eq] = "<=",
    [TekToken_greater_than_or_eq] = ">=",
    [TekToken_question_and_exclamation_mark] = "?!",
    [TekToken_double_full_stop] = "..",
    [TekToken_double_full_stop_equal] = "..=",
    [TekToken_ellipsis] = "...",
    [TekToken_double_ampersand] = "&&",
    [TekToken_double_pipe] = "||",
	[TekToken_double_less_than] = "<<",
	[TekToken_double_greater_than] = ">>",

    //
    // keywords
    //
    [TekToken_lib] = "lib",
    [TekToken_mod] = "mod",
    [TekToken_proc] = "proc",
    [TekToken_macro] = "macro",
    [TekToken_enum] = "enum",
    [TekToken_struct] = "struct",
    [TekToken_union] = "union",
    [TekToken_alias] = "alias",
    [TekToken_interf] = "interf",
    [TekToken_var] = "var",
    [TekToken_mut] = "mut",
    [TekToken_if] = "if",
    [TekToken_else] = "else",
    [TekToken_match] = "match",
    [TekToken_as] = "as",
    [TekToken_defer] = "defer",
    [TekToken_return] = "return",
    [TekToken_continue] = "continue",
    [TekToken_goto] = "goto",
    [TekToken_loop] = "loop",
    [TekToken_for] = "for",
    [TekToken_in] = "in",

    //
    // compile time
    //
    [TekToken_compile_time_if] = "$if",
    [TekToken_compile_time_match] = "$match",

    //
    // directives
    //
    [TekToken_directive_import] = "#import",
    [TekToken_directive_extern] = "#extern",
    [TekToken_directive_static] = "#static",
    [TekToken_directive_abi] = "#abi",
    [TekToken_directive_call_conv] = "#call_conv",
    [TekToken_directive_flags] = "#flags",
    [TekToken_directive_error] = "#error",
    [TekToken_directive_distinct] = "#distinct",
    [TekToken_directive_inline] = "#inline",
    [TekToken_directive_type] = "#type",
    [TekToken_directive_noalias] = "#noalias",
    [TekToken_directive_volatile] = "#volatile",
    [TekToken_directive_noreturn] = "#noreturn",
    [TekToken_directive_bitfield] = "#bitfield",
    [TekToken_directive_fallthrough] = "#fallthrough",
    [TekToken_directive_expr] = "#expr",
    [TekToken_directive_stmt] = "#stmt",
	[TekToken_directive_compound_type] = "#compound_type",
    [TekToken_directive_intrinsic] = "#intrinsic",
};

char* TekBinaryOp_strings[TekBinaryOp_COUNT] = {
	[TekBinaryOp_none] = "none",
	[TekBinaryOp_assign] = "assign",
	[TekBinaryOp_add] = "add",
	[TekBinaryOp_subtract] = "subtract",
	[TekBinaryOp_multiply] = "multiply",
	[TekBinaryOp_divide] = "divide",
	[TekBinaryOp_remainder] = "remainder",
	[TekBinaryOp_concat] = "concat",
	[TekBinaryOp_bit_and] = "bit_and",
	[TekBinaryOp_bit_or] = "bit_or",
	[TekBinaryOp_bit_xor] = "bit_xor",
	[TekBinaryOp_bit_shift_left] = "bit_shift_left",
	[TekBinaryOp_bit_shift_right] = "bit_shift_right",
	[TekBinaryOp_logical_and] = "logical_and",
	[TekBinaryOp_logical_or] = "logical_or",
	[TekBinaryOp_logical_equal] = "logical_equal",
	[TekBinaryOp_logical_not_equal] = "logical_not_equal",
	[TekBinaryOp_logical_less_than] = "logical_less_than",
	[TekBinaryOp_logical_less_than_or_equal] = "logical_less_than_or_equal",
	[TekBinaryOp_logical_greater_than] = "logical_greater_than",
	[TekBinaryOp_logical_greater_than_or_equal] = "logical_greater_than_or_equal",
	[TekBinaryOp_call] = "call",
	[TekBinaryOp_index] = "index",
	[TekBinaryOp_field_access] = "field_access",
	[TekBinaryOp_range] = "range",
	[TekBinaryOp_range_inclusive] = "range_inclusive",
	[TekBinaryOp_as] = "as",
};

char* TekUnaryOp_strings[TekUnaryOp_COUNT] = {
	[TekUnaryOp_logical_not] = "logical_not",
	[TekUnaryOp_bit_not] = "bit_not",
	[TekUnaryOp_negate] = "negate",
	[TekUnaryOp_address_of] = "address_of",
	[TekUnaryOp_dereference] = "dereference",
	[TekUnaryOp_ensure_value] = "ensure_value",
	[TekUnaryOp_ensure_null] = "ensure_null",
};

char* TekAbi_strings[TekProcCallConv_COUNT] = {
	[TekAbi_tek] = "TekAbi_tek",
	[TekAbi_c] = "TekAbi_c",
};

