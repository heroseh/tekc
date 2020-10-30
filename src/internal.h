#ifndef TEK_INTERNAL_H
#define TEK_INTERNAL_H

#include <deps/utf8proc.h>
#include <deps/cmd_arger.h>
#include <stdatomic.h>
#include "config.h"
#include "util.h"

//===========================================================================================
//
//
// Common Types & Forward Declarations
//
//
//===========================================================================================

typedef uint32_t TekCodeLocId;
typedef struct TekCodeLoc TekCodeLoc;
typedef union TekValue TekValue;
typedef_TekStk(TekValue);
typedef struct TekFile TekFile;
typedef struct TekFile* TekFilePtr;
typedef_TekStk(TekFilePtr);
typedef struct TekLib TekLib;
typedef struct TekLib* TekLibPtr;
typedef_TekStk(TekLibPtr);
typedef struct TekCompiler TekCompiler;

struct TekCodeLoc {
	TekFile* file;
	uint32_t line;
	uint32_t column;
};

typedef uint32_t TekValueId;
union TekValue {
	TekStrId str_id;
	uint64_t uint;
	int64_t sint;
	double float_;
	TekBool bool_;
	/*
	struct {
		TekStk(TekValueId) fields;
	} struct_;
	struct {
		TekStk(TekValueId) elmts;
	} array;
	*/
};

typedef uint8_t TekBinaryOp;
enum {
	TekBinaryOp_assign,
	TekBinaryOp_add,
	TekBinaryOp_subtract,
	TekBinaryOp_muliply,
	TekBinaryOp_divide,
	TekBinaryOp_remainder,
	TekBinaryOp_bit_and,
	TekBinaryOp_bit_or,
	TekBinaryOp_bit_xor,
	TekBinaryOp_bit_shift_left,
	TekBinaryOp_bit_shift_right,
	TekBinaryOp_logical_and,
	TekBinaryOp_logical_or,
	TekBinaryOp_logical_equal,
	TekBinaryOp_logical_not_equal,
	TekBinaryOp_logical_less_than,
	TekBinaryOp_logical_greater_than,
	TekBinaryOp_COUNT,
};

typedef uint8_t TekUnaryOp;
enum {
	TekUnaryOp_not,
	TekUnaryOp_addr_of,
	TekUnaryOp_deref,
	TekUnaryOp_COUNT,
};

//===========================================================================================
//
//
// Tokens - created by the Lexer
//
//
//===========================================================================================

typedef struct TekTokenLoc TekTokenLoc;
struct TekTokenLoc {
	uint32_t code_idx_start;
	uint32_t code_idx_end;
	uint32_t line;
	uint32_t column;
};

typedef uint8_t TekToken;
extern char* TekToken_strings_non_ascii[];
enum {
	//
	// start after all the ascii symbols, so these can be used in this enum
	// without any extra work.
	TekToken_ident = 128,
	TekToken_ident_abstract,

	TekToken_label,
    TekToken_end_of_file,

    //
    // literals
    //
    TekToken_lit_uint,
    TekToken_lit_sint,
    TekToken_lit_float,
    TekToken_lit_bool,
    TekToken_lit_string,

	//
    // grouped symbols
	//
    TekToken_assign_add,
    TekToken_assign_sub,
    TekToken_assign_mul,
    TekToken_assign_div,
    TekToken_assign_rem,
    TekToken_assign_bit_and,
    TekToken_assign_bit_or,
    TekToken_assign_bit_xor,
    TekToken_assign_bit_shl,
    TekToken_assign_bit_shr,
    TekToken_assign_concat,
    TekToken_concat,
    TekToken_right_arrow,
    TekToken_thick_right_arrow,
    TekToken_double_equal,
    TekToken_not_equal,
    TekToken_less_than_or_eq,
    TekToken_greater_than_or_eq,
    TekToken_question_and_exclamation_mark,
    TekToken_double_full_stop,
    TekToken_double_full_stop_equal,
    TekToken_ellipsis,
    TekToken_double_ampersand,
    TekToken_double_pipe,
	TekToken_double_less_than,
	TekToken_double_greater_than,

    //
    // keywords
    //
    TekToken_lib,
    TekToken_mod,
    TekToken_proc,
    TekToken_macro,
    TekToken_enum,
    TekToken_struct,
    TekToken_union,
    TekToken_alias,
    TekToken_interf,
    TekToken_var,
    TekToken_mut,
    TekToken_if,
    TekToken_else,
    TekToken_match,
    TekToken_as,
    TekToken_defer,
    TekToken_return,
    TekToken_break,
    TekToken_continue,
    TekToken_goto,
    TekToken_loop,
    TekToken_for,
    TekToken_in,

    //
    // compile time
    //
    TekToken_compile_time_if,
    TekToken_compile_time_match,

    //
    // directives
    //
    TekToken_directive_import,
    TekToken_directive_extern,
    TekToken_directive_static,
    TekToken_directive_abi,
    TekToken_directive_call_conv,
    TekToken_directive_flags,
    TekToken_directive_error,
    TekToken_directive_distinct,
    TekToken_directive_inline,
    TekToken_directive_type,
    TekToken_directive_noalias,
    TekToken_directive_volatile,
    TekToken_directive_noreturn,
    TekToken_directive_bitfield,
    TekToken_directive_fallthrough,
    TekToken_directive_expr,
    TekToken_directive_stmt,
	TekToken_directive_compound_type,
    TekToken_directive_intrinsic,
};

//===========================================================================================
//
//
// Lexer - takes the code and creates tokens to make it easier to parse
//
//
//===========================================================================================

typedef struct TekLexer TekLexer;
typedef struct TekTokenOpenBracket TekTokenOpenBracket;
typedef_TekStk(TekTokenOpenBracket);
typedef struct TekLexerStrEntry TekLexerStrEntry;
typedef_TekStk(TekLexerStrEntry);

struct TekTokenOpenBracket {
	TekToken token;
	uint32_t token_idx;
};

struct TekLexerStrEntry {
	union {
		uint32_t string_buf_idx; // if !is_ident
		uint32_t code_idx; // if is_ident
	};
	uint32_t str_len;
	uint32_t token_values_idx: 31;
	uint32_t is_ident: 1;
};

struct TekLexer {
	TekTokenLoc* token_locs;
	TekToken* tokens;
	uint32_t tokens_count;
	uint32_t tokens_cap;
	TekStk(uint32_t) line_code_start_indices;
	TekStk(TekValue) token_values;
	TekStk(TekTokenOpenBracket) open_brackets;
	TekStk(char) string_buf;
	TekStk(TekLexerStrEntry) str_entries;
	char* code;
	uint32_t code_len;
	uint32_t line;
	uint32_t column;
	uint32_t code_idx;
};

extern TekBool TekLexer_lex(TekLexer* lexer, TekCompiler* c, TekFile* file);

//===========================================================================================
//
//
// Abstract Syntax Tree - created by the Parser, a loose unvalidated version of the code.
//
//
//===========================================================================================

typedef struct TekAstNode TekAstNode;
typedef uint32_t TekAstNodeId;
typedef uint8_t TekAstNodeKind;
enum {
	//
	// general
	//
	TekAstNodeKind_ident,
	TekAstNodeKind_ident_abstract,
	TekAstNodeKind_anon_struct,
	TekAstNodeKind_label,

	//
	// declarations
	//
	TekAstNodeKind_lib_ref,
	TekAstNodeKind_lib_extern,
	TekAstNodeKind_mod,
	TekAstNodeKind_proc,
	TekAstNodeKind_proc_param,
	TekAstNodeKind_macro,
	TekAstNodeKind_interf,
	TekAstNodeKind_var,
	TekAstNodeKind_alias,
	TekAstNodeKind_import,
	TekAstNodeKind_import_file,
	TekAstNodeKind_type_struct,
	TekAstNodeKind_struct_field,
	TekAstNodeKind_type_enum,
	TekAstNodeKind_enum_field,
	TekAstNodeKind_type_proc,
	TekAstNodeKind_type_bounded_int,
	TekAstNodeKind_type_ptr,
	TekAstNodeKind_type_ref,
	TekAstNodeKind_type_view,
	TekAstNodeKind_type_array,
	TekAstNodeKind_type_stack,
	TekAstNodeKind_type_implicit,
	TekAstNodeKind_type_qualifier,

	//
	// statements
	//
	TekAstNodeKind_stmt_assign,
	TekAstNodeKind_stmt_return,
	TekAstNodeKind_stmt_continue,
	TekAstNodeKind_stmt_goto,
	TekAstNodeKind_stmt_defer,
	TekAstNodeKind_stmt_fallthrough,

	//
	// expressions
	//
	TekAstNodeKind_expr_op_binary,
	TekAstNodeKind_expr_op_unary,
	TekAstNodeKind_expr_if,
	TekAstNodeKind_expr_match,
	TekAstNodeKind_expr_match_case,
	TekAstNodeKind_expr_for,
	TekAstNodeKind_expr_arg_field,
	TekAstNodeKind_expr_compile_time,
	TekAstNodeKind_expr_stmt_block,
	TekAstNodeKind_expr_loop,
	TekAstNodeKind_expr_vararg_spread,
	TekAstNodeKind_expr_lit_uint,
	TekAstNodeKind_expr_lit_sint,
	TekAstNodeKind_expr_lit_float,
	TekAstNodeKind_expr_lit_bool,
	TekAstNodeKind_expr_lit_string,
	TekAstNodeKind_expr_lit_array,
};
struct TekAstNode {
	uint32_t kind: 6; // TekAstNodeKind
	// kind specific data, is signed so we get sign extension from the language when it need to be a signed field.
	int32_t kind_data: 8;
	uint32_t next_rel_idx: 18;
};


//===========================================================================================
//
//
// Parser - takes the tokens and makes an AST to make it easier to sematically validate
//
//
//===========================================================================================

typedef struct TekParser TekParser;

struct TekParser {
	uint32_t PLACEHOLDER;
};



//===========================================================================================
//
//
// Semantic Tree - a valid and structured representation of code
//
//
//===========================================================================================

/*

typedef struct TekProject TekProject;
typedef struct TekLib TekLib;
typedef struct TekEntity TekEntity;
typedef struct TekAstNode TekAstNode;
typedef struct Tek Tek;

typedef uint8_t TekEntityKind;
#define TekEntityKind_lib 0
#define TekEntityKind_mod 0
#define TekEntityKind_abstract_param 0
#define TekEntityKind_abstract_instance 0
#define TekEntityKind_struct_field 0
#define TekEntityKind_enum_entry 0
#define TekEntityKind_proc 0
#define TekEntityKind_proc_param 0
#define TekEntityKind_macro 0
#define TekEntityKind_alias 0
#define TekEntityKind_stmt 0
#define TekEntityKind_expr 0
#define TekEntityKind_type_implicit 0
#define TekEntityKind_type_implicit_uint 0
#define TekEntityKind_type_implicit_sint 0
#define TekEntityKind_type_implicit_float 0
#define TekEntityKind_type_u8 0
#define TekEntityKind_type_u16 0
#define TekEntityKind_type_u32 0
#define TekEntityKind_type_u64 0
#define TekEntityKind_type_u128 0
#define TekEntityKind_type_ureg 0
#define TekEntityKind_type_uptr 0
#define TekEntityKind_type_s8 0
#define TekEntityKind_type_s16 0
#define TekEntityKind_type_s32 0
#define TekEntityKind_type_s64 0
#define TekEntityKind_type_s128 0
#define TekEntityKind_type_sreg 0
#define TekEntityKind_type_sptr 0
#define TekEntityKind_type_f16 0
#define TekEntityKind_type_f32 0
#define TekEntityKind_type_f64 0
#define TekEntityKind_type_f128 0
#define TekEntityKind_type_d16 0
#define TekEntityKind_type_d32 0
#define TekEntityKind_type_d64 0
#define TekEntityKind_type_d128 0
#define TekEntityKind_type_bounded_int 0
#define TekEntityKind_type_ptr 0
#define TekEntityKind_type_ref 0
#define TekEntityKind_type_view 0
#define TekEntityKind_type_array 0
#define TekEntityKind_type_enum_array 0
#define TekEntityKind_type_struct 0
#define TekEntityKind_type_enum 0
#define TekEntityKind_type_proc 0
#define TekEntityKind_COUNT 0

typedef uint64_t TekEntityId;
#define tek_entity_id_counter_MASK   0xfff0000000000000
#define tek_entity_id_counter_SHIFT  48
#define tek_entity_id_idx_MASK       0x000ffffffff00000
#define tek_entity_id_idx_SHIFT      16
#define tek_entity_id_dep_idx_MASK   0x00000000000fff00
#define tek_entity_id_dep_idx_SHIFT  6
#define tek_entity_id_kind_MASK      0x00000000000000ff
#define tek_entity_id_kind_SHIFT     0

// generation counter that must match TekEntity.counter field of the indexed entityity
#define tek_entity_id_counter(id) ((id & tek_entity_id_counter_MASK) >> tek_entity_id_counter_SHIFT)

#define tek_entity_id_idx(id) ((id & tek_entity_id_idx_MASK) >> tek_entity_id_idx_SHIFT)

// dependency index
#define tek_entity_id_dep_idx(id) ((id & tek_entity_id_dep_idx_MASK) >> tek_entity_id_dep_idx_SHIFT)

// TekEntityKind
#define tek_entity_id_kind(id) ((id & tek_entity_id_kind_MASK) >> tek_entity_id_kind_SHIFT)
typedef_TekStk(TekEntityId);
typedef_TekKVStk(TekStrId, TekEntityId);

typedef struct {
	TekEntityId key;
	TekEntityId value;
} TekEntityKeyValue;
typedef_TekStk(TekEntityKeyValue);

typedef uint32_t TekEntityFlags;
#define TekEntityFlags_is_abstract 0x1

typedef TekEntityFlags TekModFlags;
#define TekModFlags_is_abstract 0x1

typedef TekEntityFlags TekStructFlags;
#define TekStructFlags_is_abstract 0x1
#define TekStructFlags_union 0x2

typedef TekEntityFlags TekStructFieldFlags;

typedef TekEntityFlags TekEnumFlags;
#define TekEnumFlags_is_abstract 0x1
#define TekEnumFlags_is_flags 0x2

typedef TekEntityFlags TekEnumEntityryFlags;
#define TekEnumEntityryFlags_is_assigned 0x1

typedef TekEntityFlags TekProcFlags;
#define TekProcFlags_is_abstract 0x1
#define TekProcFlags_is_abstract_dynamic 0x2

typedef TekEntityFlags TekAliasFlags;
#define TekAliasFlags_is_abstract 0x1
#define TekAliasFlags_is_distinct 0x2

typedef uint32_t TekPtrFlags;
#define TekPtrFlags_is_ref 0x2

typedef uint32_t TekArrayFlags;
#define TekArrayFlags_is_stack 0x2
#define TekArrayFlags_is_enum 0x4

typedef uint8_t TekStmtKind;
#define TekStmtKind_space 0
#define TekStmtKind_var_decl 0
#define TekStmtKind_assign 0
#define TekStmtKind_return 0
#define TekStmtKind_goto 0
#define TekStmtKind_label 0
#define TekStmtKind_COUNT 0

typedef uint8_t TekExprKind;
#define TekExprKind_block 0
#define TekExprKind_multiple 0
#define TekExprKind_if 0
#define TekExprKind_if_case 0
#define TekExprKind_match 0
#define TekExprKind_match_case 0
#define TekExprKind_binary 0
#define TekExprKind_unary 0
#define TekExprKind_loop 0
#define TekExprKind_call 0
#define TekExprKind_as 0
#define TekExprKind_var 0
#define TekExprKind_value 0
#define TekExprKind_COUNT 0

struct TekEntity {
	TekEntityId id;
	TekEntityId parent;
	TekLibId lib;
	TekEntityFlags flags;
	TekStrId name;
	union {
		struct {
			TekKVStk(TekStrId, TekEntityId) children;
		};
		struct {
			//
			// - abstract_params
			// - entries
			TekKVStk(TekStrId, TekEntityId) children;
			uint32_t entries_start_idx;
		} mod;
		struct {
			//
			// - abstract_params
			// - fields
			TekKVStk(TekStrId, TekEntityId) children;
			uint32_t fields_start_idx;
		} type_struct;
		struct {
			TekEntityId type;
			TekEntityId default_value;
			uint64_t offset;
		} struct_field;
		struct {
			//
			// - abstract_params
			// - entries
			TekKVStk(TekStrId, TekEntityId) children;
			TekEntityId base_type;
			TekEntityId default_value;
			uint32_t entries_start_idx;
		} type_enum;
		struct {
			TekEntityId type;
			TekEntityId value;
		} enum_entry;
		struct {
			//
			// - abstract_params
			TekKVStk(TekStrId, TekEntityId) children;
			TekEntityId block_expr;
		} macro;
		struct {
			//
			// - abstract_params
			// - params
			// - return_params
			// - variables
			// - labels
			// - block_expr
			TekKVStk(TekStrId, TekEntityId) children;
			uint16_t params_start_idx;
			uint16_t params_return_start_idx;
			uint32_t varibles_start_idx;
			uint32_t labels_start_idx;
		} proc;
		struct {
			TekEntityId type;
			TekEntityId default_value;
		} proc_param;
		struct {
			//
			// - abstract_params
			TekKVStk(TekStrId, TekEntityId) children;
			TekEntityId base_type;
		} type_alias;
		struct {
			//
			// - specialize_args
			TekKVStk(TekStrId, TekEntityId) children;
		} abstract_param;
		struct {
			TekStk(TekEntityId) args;
		} abstract_instance;
		struct {
			TekEntityId elmt_type;
		} type_ptr;
		struct {
			TekEntityId elmt_type;
			TekEntityId count;
		} type_array;
		struct {
			uint16_t bits;
			TekEntityId min;
			TekEntityId max;
		} type_bounded_int;
		struct {
			// - abstract_params
			// - params
			// - return_params
			TekKVStk(TekStrId, TekEntityId) children;
			uint16_t params_return_start_idx;
		} type_proc;
		struct {
			union {
				TekEntityId expr;
				struct {
					TekEntityId left;
					TekEntityId right;
					TekBinaryOp op;
				} assign;
				struct {
					TekEntityId init_expr;
				} var_decl;
				struct {
					TekEntityId target; // label or expr
				} goto_;
			};
			TekStmtKind kind;
		} stmt;
		struct {
			union {
				TekStk(TekEntityId) multiple;
				TekEntityId var;
				struct {
					TekStk(TekEntityId) stmts;
				} block;
				struct {
					TekStk(TekEntityKeyValue) cases;
				} if_;
				struct {
					TekEntityId cond;
					TekEntityId success_block;
				} if_case;
				struct {
					TekStk(TekEntityKeyValue) cases;
				} match_;
				struct {
					TekEntityId cond;
					TekEntityId success_block;
				} match_case;
				struct {
					TekEntityId proc;
					TekStk(TekEntityId) args;
				} call;
				struct {
					TekEntityId left;
					TekEntityId right;
					TekBinaryOp op;
				} binary;
				struct {
					TekEntityId child;
				} unary;
				struct {
					TekEntityId block;
				} loop;
				struct {
					TekEntityId expr;
					TekEntityId type;
				} as;
				struct {
					union {
						uint128_t uint;
						int128_t sint;
						double float_;
						TekStk(char) string;
						TekStk(TekEntityKeyValue) array;
						TekStk(TekEntityKeyValue) struct_fields;
					};
				} value;
			};
			TekEntityId type;
			TekExprKind kind;
		} expr;
	};
};

extern TekEntity* tek_entity_get(TekEntityId id);
*/

//===========================================================================================
//
//
// Error
//
//
//===========================================================================================

typedef uint16_t TekErrorKind;
typedef struct TekError TekError;
typedef_TekStk(TekError);
typedef struct TekErrorArg TekErrorArg;

struct TekErrorArg {
	TekFile* file;
	union {
		uint8_t byte;
		uint32_t num;
		int errnum; // will be the value of 'errno' at the time of the error.
		uint32_t token_idx;
		char* file_path;
		TekStrId str_id;
	};
};

struct TekError {
	TekErrorKind kind;
	TekErrorArg args[2];
};

enum {
	TekErrorKind_none,

	TekErrorKind_invalid_file_path, // args[0].file_path, args[1].errnum
	TekErrorKind_lib_root_file_is_used_in_another_lib, // root_file: args[0].str_id, culprit: args[1].str_id

	//
	// Lexer
	//
	TekErrorKind_lexer_file_read_failed, // args[0].file_path, args[1].errnum
	TekErrorKind_lexer_no_open_brackets_to_close, // location: args[0].token_idx
	TekErrorKind_lexer_binary_integer_can_only_have_zero_and_one, // location: args[0].token_idx
	TekErrorKind_lexer_octal_integer_has_a_max_digit_of_seven, // location: args[0].token_idx
	TekErrorKind_lexer_binary_literals_only_allow_for_int, // location: args[0].token_idx
	TekErrorKind_lexer_octal_literals_only_allow_for_int, // location: args[0].token_idx
	TekErrorKind_lexer_hex_literals_only_allow_for_int, // location: args[0].token_idx
	TekErrorKind_lexer_float_has_multiple_decimal_points, // location: args[0].token_idx
	TekErrorKind_lexer_expected_int_value_after_radix_prefix, // location: args[0].token_idx
	TekErrorKind_lexer_expected_delimiter_after_num, // location: args[0].token_idx
	TekErrorKind_lexer_overflow_uint, // location: args[0].token_idx
	TekErrorKind_lexer_overflow_sint, // location: args[0].token_idx
	TekErrorKind_lexer_overflow_float, // location: args[0].token_idx
	TekErrorKind_lexer_unclosed_string_literal, // location: args[0].token_idx
	TekErrorKind_lexer_new_line_in_a_single_line_string, // location: args[0].token_idx
	TekErrorKind_lexer_unrecognised_directive, // location: args[0].token_idx
	TekErrorKind_lexer_unsupported_token, // location: args[0].token_idx
	TekErrorKind_lexer_expected_a_compile_time_token, // location: args[0].token_idx
	TekErrorKind_lexer_unclosed_block_comment, // location: args[0].token_idx
	TekErrorKind_lexer_invalid_string_ascii_esc_char_code_fmt, // location: args[0].token_idx
	TekErrorKind_lexer_invalid_string_esc_sequence, // location: args[0].token_idx
	TekErrorKind_lexer_invalid_close_bracket, // got_location: args[0].token_idx, previously_opened_location: args[1].token_idx
	TekErrorKind_lexer_multiline_string_indent_different_char, // location: args[0].token_idx, indent_definition_location: args[1].token_idx
	TekErrorKind_lexer_multiline_string_indent_is_not_enough, // location: args[0].token_idx, indent_definition_location: args[1].token_idx
	TekErrorKind_COUNT,
};


//===========================================================================================
//
//
// Compiler
//
//
//===========================================================================================

typedef struct TekWorker TekWorker;
typedef struct TekCompileArgs TekCompileArgs;
typedef struct TekJob TekJob;
typedef_TekPool(TekJob);
typedef struct TekJobList TekJobList;
typedef struct TekJobSys TekJobSys;

//
// job types that are defined higher up will take precedence over the ones below it.
// see TekCompiler_job_next to see exactly how this works.
typedef uint8_t TekJobType;
enum {
	TekJobType_file_lex, // TekJob.file_id
	TekJobType_file_parse, // TekJob.file_id
	TekJobType_file_validate, // TekJob.file_id
	TekJobType_COUNT,
};
typedef uint32_t TekJobId;
#define TekJobId_counter_MASK  0xff000000
#define TekJobId_counter_SHIFT 24
#define TekJobId_id_MASK       0x00ffffff
#define TekJobId_id_SHIFT      0
struct TekJob {
	TekJobId next;
	TekJobType type;
	uint8_t counter;
	union {
		TekFile* file;
	};
};

struct TekJobList {
	TekJobId head;
	TekJobId tail;
};

struct TekFile {
	char* code;
	uint32_t* line_code_start_indices;
	TekTokenLoc* token_locs;
	TekToken* tokens;
	TekValue* token_values;
	uint32_t code_len;
	uint32_t tokens_count;
	uint32_t token_values_count;
	uint32_t lines_count;
	TekStrId path_str_id;

	uint32_t* ast_node_token_indexes;
	TekAstNode* ast_nodes;
	uint32_t ast_nodes_count;
	uint32_t ast_nodes_cap;
};

struct TekLib {
	TekStk(TekFilePtr) files;
	TekStk(TekLibPtr) dependers;
	TekStk(TekLibPtr) dependencies;
	TekStrId name;
};

extern thread_local TekWorker* tek_thread_worker;
struct TekWorker {
	TekCompiler* c;
	thrd_t thread;
	TekLexer lexer;
	TekParser parser;
	TekArenaAlctor arena_alctor;
};

typedef uint32_t TekCompilerFlags;
enum {
	TekCompilerFlags_is_stopping = 0x1,
	TekCompilerFlags_out_of_memory = 0x2,
};

typedef_TekKVStk(TekStrId, TekFilePtr);

struct TekCompiler {
	_Atomic TekCompilerFlags flags;
	TekCompileArgs* compile_args;
	TekStk(TekLibPtr) libs;
	TekKVStk(TekStrId, TekFilePtr) files; // TekStrId key == file path
	TekStrTab strtab;
	TekStk(TekError) errors;

	struct {
		TekSpinMtx libs;
		TekSpinMtx files;
		TekSpinMtx strtab;
		TekSpinMtx errors;
	} lock;

	struct {
		TekMtx mtx;
		_Atomic uint16_t workers_running_count;
	} wait;

	struct {
		TekPool(TekJob) pool;
		TekJobList* type_to_job_list_map;
		TekJobList* type_to_job_list_map_failed;
		uint32_t failed_count;
		uint32_t failed_count_last_iteration;
	} job_sys;

	TekWorker* workers;
	uint16_t workers_count;
	_Atomic uint16_t stalled_workers_count;
};

struct TekCompileArgs {
	char* file_path;
};

extern TekJob* TekCompiler_job_queue(TekCompiler* c, TekJobType type);
extern void TekCompiler_init(TekCompiler* c, uint32_t workers_count);
extern void TekCompiler_deinit(TekCompiler* c);
extern TekFile* TekCompiler_file_create(TekCompiler* c, char* file_path, TekStrId parent_file_path_str_id, TekBool is_lib_root);
extern void TekCompiler_lib_create(TekCompiler* c, char* root_src_file_path);
extern void TekCompiler_error_add(TekCompiler* c, TekError* error);
extern void TekCompiler_signal_stop(TekCompiler* c);
extern TekStrId TekCompiler_strtab_get_or_insert(TekCompiler* c, char* str, uint32_t str_len);
extern TekStrEntry TekCompiler_strtab_get_entry(TekCompiler* c, TekStrId str_id);
extern TekBool TekCompiler_has_errors(TekCompiler* c);

extern void TekCompiler_compile_start(TekCompiler* c, TekCompileArgs* args);
extern void TekCompiler_compile_wait(TekCompiler* c);
extern void TekCompiler_errors_string(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors);

extern noreturn void TekWorker_terminate(TekWorker* w, int exit_code);
extern noreturn void TekWorker_terminate_out_of_memory(TekWorker* w);

#endif // TEK_INTERNAL_H
