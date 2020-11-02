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

typedef uint16_t TekLibId;
typedef uint32_t TekFileId;
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
	TekFileId file_id;
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
// Memory Allocation
//
//
//===========================================================================================
#define TekLinearAlctor_cap 0x1000000000 // 10GB

#define Tek1TB   0x10000000000
#define Tek512GB 0x8000000000
#define Tek256GB 0x4000000000
#define Tek128GB 0x2000000000
#define Tek64GB  0x1000000000
#define Tek32GB  0x800000000
#define Tek16GB  0x400000000
#define Tek8GB   0x200000000
#define Tek4GB   0x100000000
#define Tek2GB   0x80000000
#define Tek1GB   0x40000000
#define Tek512MB 0x20000000
#define Tek256MB 0x10000000
#define Tek128MB 0x8000000
#define Tek64MB  0x4000000
#define Tek32MB  0x2000000
#define Tek16MB  0x1000000
#define Tek8MB   0x800000
#define Tek4MB   0x400000
#define Tek2MB   0x200000
#define Tek1MB   0x100000
#define Tek512KB 0x80000
#define Tek256KB 0x40000
#define Tek128KB 0x20000
#define Tek64KB  0x10000
#define Tek32KB  0x8000
#define Tek16KB  0x4000
#define Tek8KB   0x2000
#define Tek4KB   0x1000
#define Tek2KB   0x800
#define Tek1KB   0x400
#define Tek512B  0x200
#define Tek256B  0x100
#define Tek128B  0x80
#define Tek64B   0x40
#define Tek32B   0x20
#define Tek16B   0x10
#define Tek8B    0x8
#define Tek4B    0x4
#define Tek2B    0x2
#define Tek1B    0x1

typedef uint8_t TekMemSegCompiler;
enum {
	TekMemSegCompiler_compiler_struct, // TekCompiler
	TekMemSegCompiler_workers, // TekWorker
	TekMemSegCompiler_libs, // TekLib
	TekMemSegCompiler_file_paths, // TekStrId
	TekMemSegCompiler_files, // TekFile
	TekMemSegCompiler_strtab_hashes, // TekHash
	TekMemSegCompiler_strtab_entries, // TekStrEntry
	TekMemSegCompiler_strtab_strings, // char
	TekMemSegCompiler_jobs, // TekJob
	TekMemSegCompiler_errors, // TekError
	TekMemSegCompiler_COUNT,
};

static uintptr_t TekMemSegCompiler_sizes[TekMemSegCompiler_COUNT] = {
	[TekMemSegCompiler_compiler_struct] = Tek1MB,
	[TekMemSegCompiler_workers] = Tek1MB,
	[TekMemSegCompiler_libs] = Tek4MB,
	[TekMemSegCompiler_file_paths] = Tek1MB,
	[TekMemSegCompiler_files] = Tek16MB,
	[TekMemSegCompiler_strtab_hashes] = Tek4MB,
	[TekMemSegCompiler_strtab_entries] = Tek8MB,
	[TekMemSegCompiler_strtab_strings] = Tek8GB,
	[TekMemSegCompiler_jobs] = Tek4MB,
	[TekMemSegCompiler_errors] = Tek4MB,
};

typedef uint8_t TekMemSegLib;
enum {
	TekMemSegLib_files, // TekFileId
	TekMemSegLib_dependers, // TekLibId
	TekMemSegLib_dependencies, // TekLibId
	TekMemSegLib_COUNT,
};

static uintptr_t TekMemSegLib_sizes[TekMemSegLib_COUNT] = {
	[TekMemSegLib_files] = Tek1MB,
	[TekMemSegLib_dependers] = Tek1MB,
	[TekMemSegLib_dependencies] = Tek1MB,
};

typedef uint8_t TekMemSegFile;
enum {
	TekMemSegFile_token_locs, // TekTokenLoc
	TekMemSegFile_tokens, // TekToken
	TekMemSegFile_token_values, // TekValue
	TekMemSegFile_string_buf, // char
	TekMemSegFile_line_code_start_indices, // uintptr_t
	TekMemSegFile_ast_token_indices, // uint32_t
	TekMemSegFile_ast_nodes, // TekAstNode
	TekMemSegFile_COUNT,
};

static uintptr_t TekMemSegFile_sizes[TekMemSegFile_COUNT] = {
	[TekMemSegFile_token_locs] = Tek4GB,
	[TekMemSegFile_tokens] = Tek4GB,
	[TekMemSegFile_token_values] = Tek4GB,
	[TekMemSegFile_string_buf] = Tek4GB,
	[TekMemSegFile_line_code_start_indices] = Tek4GB,
	[TekMemSegFile_ast_token_indices] = Tek4GB,
	[TekMemSegFile_ast_nodes] = Tek4GB,
};

TekVirtMemError tek_mem_segs_reserve(uint8_t memsegs_count, uintptr_t* memsegs_sizes, void** segments_out);
TekVirtMemError tek_mem_segs_reset(uint8_t memsegs_count, uintptr_t* memsegs_sizes, void** segments);
TekVirtMemError tek_mem_segs_release(uint8_t memsegs_count, uintptr_t* memsegs_sizes, void** segments_in_out);

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
	char* code;
	uintptr_t code_len;
	uintptr_t code_idx;
	uint32_t line;
	uint32_t column;
};

extern TekBool TekLexer_lex(TekLexer* lexer, TekCompiler* c, TekFileId file_id);

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
	TekFileId file_id;
	union {
		uint8_t byte;
		uint32_t num;
		int errnum; // will be the value of 'errno' at the time of the error.
		uint32_t token_idx;
		char* file_path;
		TekStrId str_id;
		TekVirtMemError virt_mem_error;
	};
};

struct TekError {
	TekErrorKind kind;
	TekErrorArg args[2];
};

enum {
	TekErrorKind_none,
	TekErrorKind_virt_mem, // args[0].virt_mem_error

	TekErrorKind_invalid_file_path, // args[0].file_path, args[1].errnum
	TekErrorKind_lib_root_file_is_used_in_another_lib, // root_file: args[0].str_id, culprit: args[1].str_id

	//
	// Lexer
	//
	TekErrorKind_lexer_file_read_failed, // args[0].file_path, args[1].virt_mem
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
		TekFileId file_id;
	};
};

struct TekJobList {
	TekJobId head;
	TekJobId tail;
	TekSpinMtx mtx;
};

struct TekFile {
	void* segments[TekMemSegFile_COUNT];
	char* code;
	uintptr_t size;
	TekVirtMemFileHandle handle;
	TekFileId id;
	TekStrId path_str_id;
	//
	// these do not need to be atomic, since a single thread
	// will increment these apon creation.
	uint32_t tokens_count;
	uint32_t token_values_count;
	uint32_t lines_count;
	uint32_t ast_nodes_count;
};

static inline TekTokenLoc* TekFile_token_locs(TekFile* file) { return file->segments[TekMemSegFile_token_locs]; }
static inline TekToken* TekFile_tokens(TekFile* file) { return file->segments[TekMemSegFile_tokens]; }
static inline TekValue* TekFile_token_values(TekFile* file) { return file->segments[TekMemSegFile_token_values]; }
static inline char* TekFile_string_buf(TekFile* file) { return file->segments[TekMemSegFile_string_buf]; }
static inline uintptr_t* TekFile_line_code_start_indices(TekFile* file) { return file->segments[TekMemSegFile_line_code_start_indices]; }
static inline uint32_t* TekFile_ast_token_indices(TekFile* file) { return file->segments[TekMemSegFile_ast_token_indices]; }
static inline TekAstNode* TekFile_ast_nodes(TekFile* file) { return file->segments[TekMemSegFile_ast_nodes]; }

struct TekLib {
	void* segments[TekMemSegLib_COUNT];
	_Atomic uint32_t files_count;
	_Atomic uint32_t dependers_count;
	_Atomic uint32_t dependencies_count;
	TekLibId id;
	TekStrId name;
};

static inline TekFileId* TekLib_files(TekLib* lib) { return lib->segments[TekMemSegLib_files]; }
static inline TekLibId* TekLib_dependers(TekLib* lib) { return lib->segments[TekMemSegLib_dependers]; }
static inline TekLibId* TekLib_dependencies(TekLib* lib) { return lib->segments[TekMemSegLib_dependencies]; }

struct TekWorker {
	TekCompiler* c;
	thrd_t thread;
	TekLexer lexer;
	TekParser parser;
};

typedef uint32_t TekCompilerFlags;
enum {
	TekCompilerFlags_is_stopping = 0x1,
	TekCompilerFlags_out_of_memory = 0x2,
	TekCompilerFlags_starting_up = 0x4,
	TekCompilerFlags_is_running = 0x8,
};

typedef_TekKVStk(TekStrId, TekFilePtr);

struct TekCompiler {
	_Atomic TekCompilerFlags flags;
	uint16_t workers_count;
	_Atomic uint16_t stalled_workers_count;
	_Atomic uint16_t running_workers_count;
	TekCompileArgs* compile_args;

	void* segments[TekMemSegCompiler_COUNT];
	_Atomic uint32_t libs_count;
	_Atomic uint32_t files_count;
	_Atomic uint32_t jobs_count;
	_Atomic uint32_t errors_count;
	_Atomic uint32_t strtab_entries_count;
	_Atomic uintptr_t strtab_strings_size;

	TekMtx wait_mtx;

	struct {
		_Atomic uint32_t available_count;
		_Atomic uint32_t failed_count;
		uint32_t failed_count_last_iteration;
		TekJobList free_list;
		TekJobList type_to_job_list_map[TekJobType_COUNT];
		TekJobList type_to_job_list_map_failed[TekJobType_COUNT];
	} job_sys;
};

static inline TekWorker* TekCompiler_workers(TekCompiler* c) { return c->segments[TekMemSegCompiler_workers]; }
static inline TekLib* TekCompiler_libs(TekCompiler* c) { return c->segments[TekMemSegCompiler_libs]; }
static inline _Atomic TekStrId* TekCompiler_file_paths(TekCompiler* c) { return c->segments[TekMemSegCompiler_file_paths]; }
static inline TekFile* TekCompiler_files(TekCompiler* c) { return c->segments[TekMemSegCompiler_files]; }
static inline _Atomic TekHash* TekCompiler_strtab_hashes(TekCompiler* c) { return c->segments[TekMemSegCompiler_strtab_hashes]; }
static inline _Atomic TekStrEntry* TekCompiler_strtab_entries(TekCompiler* c) { return c->segments[TekMemSegCompiler_strtab_entries]; }
static inline char* TekCompiler_strtab_strings(TekCompiler* c) { return c->segments[TekMemSegCompiler_strtab_strings]; }
static inline TekJob* TekCompiler_jobs(TekCompiler* c) { return c->segments[TekMemSegCompiler_jobs]; }
static inline TekError* TekCompiler_errors(TekCompiler* c) { return c->segments[TekMemSegCompiler_errors]; }

struct TekCompileArgs {
	char* file_path;
};

typedef uint8_t TekCompilerError;
enum {
	TekCompilerError_none,
	TekCompilerError_already_running,
	TekCompilerError_failed_to_start_worker_threads,
	TekCompilerError_compile_error,
};

extern TekJob* TekCompiler_job_queue(TekCompiler* c, TekJobType type);
extern TekCompiler* TekCompiler_init();
extern void TekCompiler_deinit(TekCompiler* c);
extern TekFileId TekCompiler_file_create(TekCompiler* c, char* file_path, TekStrId parent_file_path_str_id, TekBool is_lib_root);
extern TekFile* TekCompiler_file_get(TekCompiler* c, TekFileId file_id);
extern TekLibId TekCompiler_lib_create(TekCompiler* c, char* root_src_file_path);
extern TekLib* TekCompiler_lib_get(TekCompiler* c, TekLibId lib_id);
extern TekError* TekCompiler_error_add(TekCompiler* c, TekErrorKind kind);
extern void TekCompiler_signal_stop(TekCompiler* c);
extern TekStrId TekCompiler_strtab_get_or_insert(TekCompiler* c, char* str, uint32_t str_len);
extern TekStrEntry TekCompiler_strtab_get_entry(TekCompiler* c, TekStrId str_id);
extern TekBool TekCompiler_has_errors(TekCompiler* c);
extern TekBool TekCompiler_out_of_memory(TekCompiler* c);

TekCompilerError TekCompiler_compile_start(TekCompiler* c, uint16_t workers_count, TekCompileArgs* args);
extern TekCompilerError TekCompiler_compile_wait(TekCompiler* c);
extern void TekCompiler_errors_string(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors);

#endif // TEK_INTERNAL_H
