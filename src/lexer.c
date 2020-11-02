#include "internal.h"

static uint32_t TekLexer_identifier_byte_count(TekLexer* lexer) {
	uint32_t token_byte_count = 0;
	uint8_t* pos = (uint8_t*)lexer->code + lexer->code_idx;
	uint32_t remaining_count = lexer->code_len - lexer->code_idx;
	while (1) {
		int32_t codept;
		intptr_t codept_byte_count = utf8proc_iterate(pos + token_byte_count, remaining_count - token_byte_count, &codept);
		if (codept_byte_count < 0) {
			return 0;
		}

		TekBool found_delimiter = tek_false;
		utf8proc_category_t utf8_category = utf8proc_category(codept);
		switch (utf8_category) {
			case UTF8PROC_CATEGORY_LU: /**< Letter, uppercase */
			case UTF8PROC_CATEGORY_LL: /**< Letter, lowercase */
			case UTF8PROC_CATEGORY_LT: /**< Letter, titlecase */
			case UTF8PROC_CATEGORY_LM: /**< Letter, modifier */
			case UTF8PROC_CATEGORY_LO: /**< Letter, other */
			case UTF8PROC_CATEGORY_ND: /**< Number, decimal digit */
			case UTF8PROC_CATEGORY_NL: /**< Number, letter */
			case UTF8PROC_CATEGORY_NO: /**< Number, other */
				break;
			case UTF8PROC_CATEGORY_PC: /**< Punctuation, connector */
				if (codept == U'_') {
					break;
				}
				// fallthrough
			case UTF8PROC_CATEGORY_PD: /**< Punctuation, dash */
			case UTF8PROC_CATEGORY_PS: /**< Punctuation, open */
			case UTF8PROC_CATEGORY_PE: /**< Punctuation, close */
			case UTF8PROC_CATEGORY_PI: /**< Punctuation, initial quote */
			case UTF8PROC_CATEGORY_PF: /**< Punctuation, final quote */
			case UTF8PROC_CATEGORY_PO: /**< Punctuation, other */
			case UTF8PROC_CATEGORY_SM: /**< Symbol, math */
			case UTF8PROC_CATEGORY_SC: /**< Symbol, currency */
			case UTF8PROC_CATEGORY_SK: /**< Symbol, modifier */
			case UTF8PROC_CATEGORY_SO: /**< Symbol, other */
			case UTF8PROC_CATEGORY_ZS: /**< Separator, space */
			case UTF8PROC_CATEGORY_ZL: /**< Separator, line */
			case UTF8PROC_CATEGORY_ZP: /**< Separator, paragraph */
			{
				if (token_byte_count == 0) {
					return 0;
				}

				found_delimiter = tek_true;
				break;
			}

			case UTF8PROC_CATEGORY_CC: /**< Other, control */
				if (codept == U'\n' || codept == U'\r' || codept == U'\t') {
					found_delimiter = tek_true;
					break;
				}
				// fallthrough
			case UTF8PROC_CATEGORY_MN: /**< Mark, nonspacing */
			case UTF8PROC_CATEGORY_MC: /**< Mark, spacing combining */
			case UTF8PROC_CATEGORY_ME: /**< Mark, enclosing */
			case UTF8PROC_CATEGORY_CN: /**< Other, not assigned */
			case UTF8PROC_CATEGORY_CF: /**< Other, format */
			case UTF8PROC_CATEGORY_CS: /**< Other, surrogate */
			case UTF8PROC_CATEGORY_CO: /**< Other, private use */
				return 0;
		}

		if (found_delimiter) {
			break;
		}

		token_byte_count += codept_byte_count;
		if (token_byte_count >= remaining_count) {
			break;
		}
	}

	return token_byte_count;
}

static inline TekBool _TekLexer_has_code(TekLexer* lexer) {
	return lexer->code_idx < lexer->code_len;
}

static inline uint8_t _TekLexer_peek_byte(TekLexer* lexer) {
	return lexer->code[lexer->code_idx];
}

static inline uint8_t _TekLexer_peek_byte_ahead(TekLexer* lexer, uint32_t by) {
	return lexer->code[lexer->code_idx + by];
}

static inline void _TekLexer_advance_column(TekLexer* lexer, uint32_t by) {
	lexer->code_idx += by;
	lexer->column += by;
}

static inline void _TekLexer_advance_line(TekLexer* lexer, TekFile* file) {
	uint8_t byte = _TekLexer_peek_byte(lexer);
	if (byte == '\r') {
		lexer->code_idx += 1;
		if (!_TekLexer_has_code(lexer)) return;
		byte = _TekLexer_peek_byte(lexer);
	}

	if (byte == '\n') {
		lexer->code_idx += 1;
	}

	//printf("line %u: %.*s\n", lexer->line + 1, 10, lexer->code + lexer->code_idx);

	uintptr_t* line_code_start_indices = TekFile_line_code_start_indices(file);
	line_code_start_indices[file->lines_count] = lexer->code_idx;
	file->lines_count += 1;
	lexer->line += 1;
	lexer->column = 1;
}

#define _TekLexer_compare_consume_lit(lexer, str) _TekLexer_compare_consume(lexer, str, sizeof(str) - 1)
static TekBool _TekLexer_compare_consume(TekLexer* lexer, char* str, uint32_t str_len) {
	char* cursor = lexer->code + lexer->code_idx;
	if (str_len > lexer->code_len - lexer->code_idx) return tek_false;
	uint32_t remaining = str_len;
	while (remaining) {
		if (*cursor != *str) return tek_false;
		cursor += 1;
		str += 1;
		remaining -= 1;
	}
	_TekLexer_advance_column(lexer, str_len);
	return tek_true;
}

void TekLexer_token_add(TekLexer* lexer, TekFile* file, TekToken token, uint32_t code_idx_start, uint32_t code_idx_end, uint32_t line_start, uint32_t column_start) {
	TekTokenLoc* token_locs = TekFile_token_locs(file);
	TekToken* tokens = TekFile_tokens(file);

	//
	// insert the token and it's location into the arrays
	uint32_t insert_idx = file->tokens_count;
	token_locs[insert_idx] = (TekTokenLoc) {
		.code_idx_start = code_idx_start,
		.code_idx_end = code_idx_end,
		.line = line_start,
		.column = column_start,
	};

	tokens[insert_idx] = token;
	file->tokens_count += 1;
}

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
    [TekToken_assign_sub] = "-=",
    [TekToken_assign_mul] = "*=",
    [TekToken_assign_div] = "/=",
    [TekToken_assign_rem] = "%=",
    [TekToken_assign_bit_and] = "&=",
    [TekToken_assign_bit_or] = "|=",
    [TekToken_assign_bit_xor] = "^=",
    [TekToken_assign_bit_shl] = "<<=",
    [TekToken_assign_bit_shr] = ">>=",
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

TekBool TekLexer_lex(TekLexer* lexer, TekCompiler* c, TekFileId file_id) {
	TekTokenOpenBracket open_brackets[tek_lexer_cap_open_brackets] = {0};
	uint32_t open_brackets_count = 0;

	//
	// reset the lexer
	tek_zero_elmt(lexer);
	TekError error = {0};

	TekFile* file = TekCompiler_file_get(c, file_id);

	//
	// setup the lexer to use the file
	lexer->code = file->code;
	lexer->code_len = file->size;

#define bail(kind_) \
	{ \
		error.kind = kind_; \
		goto BAIL; \
	}

	uint32_t code_idx_start, line_start, column_start;
	char num_buf[128];
	TekToken open_variant;
	TekBool is_signed;
	TekToken token;
	TekTokenLoc* token_locs = TekFile_token_locs(file);
	TekToken* tokens = TekFile_tokens(file);
	TekValue* token_values = TekFile_token_values(file);
	uintptr_t* line_code_start_indices = TekFile_line_code_start_indices(file);
	char* string_buf = TekFile_string_buf(file);
	uintptr_t string_buf_size;
	while (_TekLexer_has_code(lexer)) {
		token = _TekLexer_peek_byte(lexer);
		code_idx_start = lexer->code_idx;
		line_start = lexer->line;
		column_start = lexer->column;

		switch (token) {
			case ' ':
				// TODO: legend has it... we can use SIMD to speed this up.
				do { _TekLexer_advance_column(lexer, 1); }
					while (_TekLexer_has_code(lexer) && _TekLexer_peek_byte(lexer) == ' ');
				continue;
			case '\t':
				// TODO: legend has it... we can use SIMD to speed this up.
				do { _TekLexer_advance_column(lexer, 1); }
					while (_TekLexer_has_code(lexer) && _TekLexer_peek_byte(lexer) == '\t');
				continue;
			//
			// simple ascii symbols that are not the start of a grouped symbol
			//
			case ',':
			case ':':
			case '@':
			case '~':
			case '\\':
				break;

			case '.':
				if (_TekLexer_compare_consume_lit(lexer, "...")) token = TekToken_ellipsis;
				else if (_TekLexer_compare_consume_lit(lexer, "..=")) token = TekToken_double_full_stop_equal;
				else if (_TekLexer_compare_consume_lit(lexer, "..")) token = TekToken_double_full_stop;
				break;

			//
			// any of the newline terminators
			case ';':
			case '\r':
			case '\n':
				do {
					switch (_TekLexer_peek_byte(lexer)) {
						case ';': _TekLexer_advance_column(lexer, 1); break;
						case '\r':
						case '\n': _TekLexer_advance_line(lexer, file); break;
						default: goto NEW_LINE_END;
					}
				} while (_TekLexer_has_code(lexer));
NEW_LINE_END:
				//
				// block comment will allow a newline to be made twice, so combine them if this happens.
				if (file->tokens_count > 0 && tokens[file->tokens_count - 1] == '\n') {
					TekTokenLoc* prev_loc = &token_locs[file->tokens_count - 1];
					prev_loc->code_idx_end = lexer->code_idx;
					continue;
				}
				token = '\n';
				break;

			//
			// any of the brackets
			case '(':
			case '{':
			case '[': {
				tek_assert(open_brackets_count < tek_lexer_cap_open_brackets, "maximum number of open brackets has been reached: %u", tek_lexer_cap_open_brackets);
				open_brackets[open_brackets_count] =
					(TekTokenOpenBracket){ .token = token, .token_idx = file->tokens_count };
				open_brackets_count += 1;
				break;
			};
			case ')':
				open_variant = '(';
				goto CHECK_CLOSE_BRACKET;
			case '}':
				open_variant = '{';
				goto CHECK_CLOSE_BRACKET;
			case ']': {
				open_variant = '[';
CHECK_CLOSE_BRACKET:
				if (open_brackets_count == 0)
					bail(TekErrorKind_lexer_no_open_brackets_to_close);

				if (open_brackets[open_brackets_count - 1].token != open_variant) {
					goto BAIL_INCORRECT_CLOSE_BRACKET;
				}
				open_brackets_count -= 1;
				break;
			};

			//
			// uint, sint and float literals
			//
			case '0' ... '9': {
				is_signed = tek_false;
TOKEN_NUM: {}
		   		if (is_signed) {
					token = TekToken_lit_sint;
				} else {
					token = TekToken_lit_uint;
				}
				uint8_t radix = 10;
				if (_TekLexer_compare_consume_lit(lexer, "0x")) { // is hex
					radix = 16;
				} else if (_TekLexer_compare_consume_lit(lexer, "0o")) { // is octal
					radix = 8;
				} else if (_TekLexer_compare_consume_lit(lexer, "0b")) { // is binary
					radix = 2;
				}

				//
				// analyze and validate whether this is an integer or a float.
				// we copy the characters to a buffer to filter out any underscores.
				// this following errors are handle below:
				// - 0x and 0b prefix without digit following after
				// - 0x and 0b do not work for floats
				// - floats with multiple decimal points
				// - overflowing the number buffer
				// - any non symbol and non whitespace character trying to terminate a number
				uint32_t num_buf_count = 0;
				if (is_signed) {
					num_buf_count = 1;
					num_buf[0] = '-';
				}
				do {
					uint8_t byte = _TekLexer_peek_byte(lexer);
					switch (byte) {
						// underscores are not processed into the number buffer so skip to the next byte.
						case '_': goto NUM_CONTINUE;
						case '0' ... '9':
							if (radix == 2 && byte > '1') {
								code_idx_start = lexer->code_idx;
								bail(TekErrorKind_lexer_binary_integer_can_only_have_zero_and_one);
							}
							if (radix == 8 && byte > '7') {
								code_idx_start = lexer->code_idx;
								bail(TekErrorKind_lexer_octal_integer_has_a_max_digit_of_seven);
							}
							break;
						case '.':
							//
							// if there is no digits after the full stop
							// then stop processing this num.
							// the full stop will not be processed either.
							//
							if (
								lexer->code_len - lexer->code_idx <= 1 ||
								_TekLexer_peek_byte_ahead(lexer, 1) < '0' ||
								_TekLexer_peek_byte_ahead(lexer, 1) > '9'
							) {
								goto NUM_ANALYZE_END;
							}

							if (radix == 2) bail(TekErrorKind_lexer_binary_literals_only_allow_for_int);
							if (radix == 8) bail(TekErrorKind_lexer_octal_literals_only_allow_for_int);
							if (radix == 16) bail(TekErrorKind_lexer_hex_literals_only_allow_for_int);
							if (token == TekToken_lit_float) {
								code_idx_start = lexer->code_idx;
								bail(TekErrorKind_lexer_float_has_multiple_decimal_points);
							}

							token = TekToken_lit_float;
							break;
						case 'A' ... 'F':
						case 'a' ... 'f':
							if (radix == 16) break;
							// fallthrough for non hex values
						default:
							code_idx_start = lexer->code_idx;
							if (num_buf_count == 0) {
								// is_signed cannot get here as it checks
								// for a digit before coming here
								bail(TekErrorKind_lexer_expected_int_value_after_radix_prefix);
							}
							bail(TekErrorKind_lexer_expected_delimiter_after_num);

						//
						// we are just allowing all symbols and escape codes
						// in the ASCII table to teriminate numbers.
						case '\0' ... '-':
						case '/':
						case ':' ... '@':
						case '[' ... '^':
						case '`':
						case '{' ... '~':
							if (num_buf_count == 0) {
								// is_signed cannot get here as it checks
								// for a digit before coming here
								bail(TekErrorKind_lexer_expected_int_value_after_radix_prefix);
							}
							goto NUM_ANALYZE_END;
					}

					if (num_buf_count == sizeof(num_buf)) {
NUM_OVERFLOW:
						switch (token) {
							case TekToken_lit_uint: bail(TekErrorKind_lexer_overflow_uint);
							case TekToken_lit_sint: bail(TekErrorKind_lexer_overflow_sint);
							case TekToken_lit_float: bail(TekErrorKind_lexer_overflow_float);
							default: tek_abort("only expected uint, sint or float");
						}
					}

					num_buf[num_buf_count] = byte;
					num_buf_count += 1;
NUM_CONTINUE:
					_TekLexer_advance_column(lexer, 1);
				} while (_TekLexer_has_code(lexer));
NUM_ANALYZE_END: {}

				num_buf[num_buf_count] = '\0';
				TekValue* value = &token_values[file->token_values_count];
				file->token_values_count += 1;
				char* end_ptr = NULL;
				switch (token) {
					case TekToken_lit_uint:
						value->uint = strtoul(num_buf, &end_ptr, radix);
						if ((value->uint == 0 || value->uint == ULONG_MAX) && errno == ERANGE) {
							goto NUM_OVERFLOW;
						}
						break;
					case TekToken_lit_sint:
						value->sint = strtol(num_buf, &end_ptr, radix);
						if ((value->sint == LONG_MIN || value->sint == LONG_MAX) && errno == ERANGE) {
							goto NUM_OVERFLOW;
						}
						break;
					case TekToken_lit_float:
						value->float_ = strtod(num_buf, &end_ptr);
						if ((value->float_ == -HUGE_VAL || value->float_ == HUGE_VAL) && errno == ERANGE) {
							goto NUM_OVERFLOW;
						}
						break;
					default: tek_abort("only expected uint, sint or float tokens");
				}

				if (end_ptr - num_buf != num_buf_count) {
					goto NUM_OVERFLOW;
				}
				break;
			};

			//
			// string literals
			//
			case '"': {
				token = TekToken_lit_string;
				string_buf_size = 0;

				_TekLexer_advance_column(lexer, 1);

				// error: unclosed string literal right at the end of the file
				if (!_TekLexer_has_code(lexer)) { bail(TekErrorKind_lexer_unclosed_string_literal); }


				uint8_t byte = _TekLexer_peek_byte(lexer);
				TekBool allow_new_line = tek_false;
				char indent_char = 0;
				uint32_t indent_line = 0;
				//
				// when a string starts with a new line, we ignore all of the leading whitespace.
				// also strings that start with a new line can have new lines in them.
				if (byte == '\r' || byte == '\n') {
					allow_new_line = tek_true;
					_TekLexer_advance_line(lexer, file);
					indent_char = _TekLexer_peek_byte(lexer);
					while (1) {
						if (!_TekLexer_has_code(lexer)) bail(TekErrorKind_lexer_unclosed_string_literal);
						byte = _TekLexer_peek_byte(lexer);
						if (byte == '\r' || byte == '\n') {
							_TekLexer_advance_line(lexer, file);
							string_buf[string_buf_size] = byte;
							string_buf_size += 1;
							indent_char = _TekLexer_peek_byte(lexer);
						} else if (byte == ' ' || byte == '\t') {
							if (byte != indent_char)
								break;
							_TekLexer_advance_column(lexer, 1);
						} else {
							break;
						}
					}
					indent_line = lexer->line;
				}


				//
				// process byte by byte, copying to the string buf and handling escaped characters
				uint32_t newline_ignore_whitespace_upto = lexer->column;
				while (1) {
					if (allow_new_line) {
						while (lexer->column < newline_ignore_whitespace_upto) {
							if (!_TekLexer_has_code(lexer)) bail(TekErrorKind_lexer_unclosed_string_literal);
							byte = _TekLexer_peek_byte(lexer);
							if (byte == '"') break;
							if (byte == '\r' || byte == '\n') {
								_TekLexer_advance_line(lexer, file);
								string_buf[string_buf_size] = byte;
								string_buf_size += 1;
							} else if (byte == ' ' || byte == '\t') {
								if (byte != indent_char) {
									error.kind = TekErrorKind_lexer_multiline_string_indent_different_char;
									goto STRING_ERR;
								}
								_TekLexer_advance_column(lexer, 1);
							} else {
								error.kind = TekErrorKind_lexer_multiline_string_indent_is_not_enough;
STRING_ERR:
								// push a dummy token where the indentation was defined
								error.args[1].file_id = file_id;
								error.args[1].token_idx = file->tokens_count;
								code_idx_start = line_code_start_indices[indent_line - 1];
								TekLexer_token_add(lexer, file, 0, code_idx_start, code_idx_start + newline_ignore_whitespace_upto - 1, indent_line, 1);

								code_idx_start = line_code_start_indices[indent_line - 1];
								line_start = lexer->line;
								bail(error.kind);
							}
						}
					}

					uint8_t byte = _TekLexer_peek_byte(lexer);
					if (byte == '"') {
						//
						// for multiline strings, we allow the string to be terminated on the
						// next line without including the new line character.
						// so if the string terminator is before the indent of the multiline string.
						// return the new line character/s.
						//
						if (allow_new_line && lexer->column < newline_ignore_whitespace_upto && string_buf_size) {
							char byte = string_buf[string_buf_size - 1];
							if (byte == '\n') {
								string_buf_size -= 1;
								if (string_buf_size) {
									byte = string_buf[string_buf_size - 1];
									if (byte == '\r') {
										string_buf_size -= 1;
									}
								}
							}
						}
						_TekLexer_advance_column(lexer, 1);
						break;
					} else if (byte == '\r' || byte == '\n') {
						if (!allow_new_line) {
							code_idx_start = lexer->code_idx;
							bail(TekErrorKind_lexer_new_line_in_a_single_line_string);
						}
						_TekLexer_advance_line(lexer, file);
						goto STRING_CONTINUE;
					} else if (byte == '\\') { // escape codes
						_TekLexer_advance_column(lexer, 1);
						if (!_TekLexer_has_code(lexer)) { bail(TekErrorKind_lexer_unclosed_string_literal); }
						byte = _TekLexer_peek_byte(lexer);
						switch (byte) {
							case 'n': byte = '\n'; break;
							case 'r': byte = '\r'; break;
							case 't': byte = '\t'; break;
							case '"': byte = '\"'; break;
							case '\\': byte = '\\'; break;
							case '0': byte = '\0'; break;
							case 'x':
								//
								// process both digits
								byte = 0;
								for (uint32_t i = 0; i < 2; i += 1) {
									//
									// second hex digit
									_TekLexer_advance_column(lexer, 1);
									if (!_TekLexer_has_code(lexer)) { bail(TekErrorKind_lexer_unclosed_string_literal); }
									char next_byte = _TekLexer_peek_byte(lexer);
									switch (next_byte) {
										case '0' ... '9':
											byte += next_byte - '0';
											break;
										case 'a' ... 'f':
											byte += (next_byte - 'a') + 10;
											break;
										case 'A' ... 'F':
											byte += (next_byte - 'A') + 10;
											break;
										default:
											code_idx_start = lexer->code_idx;
											bail(TekErrorKind_lexer_invalid_string_ascii_esc_char_code_fmt);
									}

									// if first digit move over to the next hex column
									if (i == 0)
										byte = (byte - '0') * 16;
								}
								break;
							default:
								code_idx_start = lexer->code_idx;
								bail(TekErrorKind_lexer_invalid_string_esc_sequence);
						}
					}

					_TekLexer_advance_column(lexer, 1);
STRING_CONTINUE: {}
					if (!_TekLexer_has_code(lexer)) { bail(TekErrorKind_lexer_unclosed_string_literal); }
				}

				//
				// deduplicate the string using the compiler's global string table.
				// this will allow us to only compare an integer to check for string equality.
				TekValue* value = &token_values[file->token_values_count];
				file->token_values_count += 1;
				value->str_id = TekCompiler_strtab_get_or_insert(c, string_buf, string_buf_size);
				break;
			};
			//
			// symbols & grouped symbols
			//
			case '+': {
				if (_TekLexer_compare_consume_lit(lexer, "+=")) token = TekToken_assign_add;
				else if (_TekLexer_compare_consume_lit(lexer, "++")) token = TekToken_concat;
				else if (_TekLexer_compare_consume_lit(lexer, "++=")) token = TekToken_assign_concat;
				break;
			};
			case '-': {
				if (_TekLexer_compare_consume_lit(lexer, "-=")) token = TekToken_assign_sub;
				else if (_TekLexer_compare_consume_lit(lexer, "->")) token = TekToken_right_arrow;
				else if (lexer->code_idx + 1 < lexer->code_len) {
					uint8_t next_byte = lexer->code[lexer->code_idx + 1];
					if (next_byte >= '0' && next_byte <= '9') {
						// special case where the minus is used to represent a negative number literal
						_TekLexer_advance_column(lexer, 1);
						is_signed = tek_true;
						goto TOKEN_NUM;
					}
				}
				break;
			};
			case '*': {
				if (_TekLexer_compare_consume_lit(lexer, "*=")) token = TekToken_assign_mul;
				break;
			};
			case '/': {
				if (_TekLexer_compare_consume_lit(lexer, "/=")) {
					token = TekToken_assign_div;
				} else if (_TekLexer_compare_consume_lit(lexer, "//")) { // line comment
					//
					// skip over every character that comes before the next new line.
					// this is so the new line get tokenized on the next iteration.
					// but we skip over the new line if the previous token was a new line.
					//
					uint8_t byte = '\0';
					while (_TekLexer_has_code(lexer)) {
						byte = _TekLexer_peek_byte(lexer);
						if (byte == '\n' || byte == '\r') {
							if (file->tokens_count > 0 && tokens[file->tokens_count - 1] == '\n') {
								_TekLexer_advance_line(lexer, file);
							}
							break;
						}
						_TekLexer_advance_column(lexer, 1);
					}

					// line comment is not a token, so continue
					continue;
				} else if (_TekLexer_compare_consume_lit(lexer, "/*")) { // block comment
					uint32_t nested_count = 0;
					uint8_t last_byte = '\0';
					//
					// skip over every character until we come across the matching terminator: */
					// this accounts for nested block comments, so if we find another: /*
					// we have to look for another: */
					while (1) {
						if (!_TekLexer_has_code(lexer)) {
							lexer->code_idx = code_idx_start + 2;
							bail(TekErrorKind_lexer_unclosed_block_comment);
						}
						uint8_t byte = _TekLexer_peek_byte(lexer);
						if (last_byte == '/' && byte == '*') {
							nested_count += 1;
							// make the byte null to avoid a slash directly after terminating the comment.
							// eg. to avoid this: /*/
							byte = '\0';
						} else if (last_byte == '*' && byte == '/') {
							if (nested_count == 0) {
								_TekLexer_advance_column(lexer, 1);
								break;
							} else {
								nested_count -= 1;
							}
						} else if (byte == '\r' || byte == '\n') {
							_TekLexer_advance_line(lexer, file);
							last_byte = byte;
							continue;
						}
						_TekLexer_advance_column(lexer, 1);
						last_byte = byte;
					}

					// block comment is not a token, so continue
					continue;
				}
				break;
			};
			case '%': {
				if (_TekLexer_compare_consume_lit(lexer, "%=")) token = TekToken_assign_rem;
				break;
			};
			case '!': {
				if (_TekLexer_compare_consume_lit(lexer, "!=")) token = TekToken_not_equal;
				break;
			};
			case '&': {
				if (_TekLexer_compare_consume_lit(lexer, "&&")) token = TekToken_double_ampersand;
				else if (_TekLexer_compare_consume_lit(lexer, "&=")) token = TekToken_assign_bit_and;
				break;
			};
			case '|': {
				if (_TekLexer_compare_consume_lit(lexer, "||")) token = TekToken_double_pipe;
				else if (_TekLexer_compare_consume_lit(lexer, "|=")) token = TekToken_assign_bit_or;
				break;
			};
			case '^':
				if (_TekLexer_compare_consume_lit(lexer, "^=")) token = TekToken_assign_bit_xor;
				break;
			case '<': {
				if (_TekLexer_compare_consume_lit(lexer, "<=")) token = TekToken_greater_than_or_eq;
				else if (_TekLexer_compare_consume_lit(lexer, "<<=")) token = TekToken_assign_bit_shl;
				else if (_TekLexer_compare_consume_lit(lexer, "<<")) token = TekToken_double_greater_than;
				break;
			};
			case '>': {
				if (_TekLexer_compare_consume_lit(lexer, ">=")) token = TekToken_less_than_or_eq;
				else if (_TekLexer_compare_consume_lit(lexer, ">>=")) token = TekToken_assign_bit_shr;
				else if (_TekLexer_compare_consume_lit(lexer, ">>")) token = TekToken_double_less_than;
				break;
			};
			case '=': {
				if (_TekLexer_compare_consume_lit(lexer, "==")) token = TekToken_double_equal;
				else if (_TekLexer_compare_consume_lit(lexer, "=>")) token = TekToken_thick_right_arrow;
				break;
			};
			case '?': {
				if (_TekLexer_compare_consume_lit(lexer, "?!")) token = TekToken_question_and_exclamation_mark;
				break;
			};
			case '$': {
				if (_TekLexer_compare_consume_lit(lexer, "$if")) token = TekToken_compile_time_if;
				else if (_TekLexer_compare_consume_lit(lexer, "$match")) token = TekToken_compile_time_match;
				else {
					token = TekToken_ident_abstract;

					_TekLexer_advance_column(lexer, 1);
					uint32_t ident_len = TekLexer_identifier_byte_count(lexer);

					if (ident_len == 0) {
						bail(TekErrorKind_lexer_expected_a_compile_time_token);
					} else {
						//
						// deduplicate the identifier using the compiler's global string table.
						// this will allow us to only compare an integer to check for string equality.
						TekValue* value = &token_values[file->token_values_count];
						file->token_values_count += 1;
						// - 1 to include the $ symbol
						value->str_id = TekCompiler_strtab_get_or_insert(c, lexer->code + lexer->code_idx - 1, ident_len);
						_TekLexer_advance_column(lexer, ident_len);
					}
				}
				break;
			};

			//
			// label, identifier, directives and keywords
			//
			case '#':
			case '\'':
				_TekLexer_advance_column(lexer, 1);
				// fallthrough
			default: {
				uint32_t ident_len = TekLexer_identifier_byte_count(lexer);
				if (ident_len == 0) {
					bail(TekErrorKind_lexer_unsupported_token);
				}

				TekBool is_directive = token == '#';
				if (is_directive) {
					// put the cursor back on the #
					// so we can compare with it in the strings below.
					lexer->code_idx -= 1;
					lexer->column -= 1;
					ident_len += 1;
				}

				token = TekToken_ident;

				uint32_t ident_start_idx = lexer->code_idx;

				if (token == '\'') {
					token = TekToken_label;
				} else { // else identifier or directive
					switch (ident_len) {
						case 2:
							if (_TekLexer_compare_consume_lit(lexer, "if")) {
								token = TekToken_if;
							} else if (_TekLexer_compare_consume_lit(lexer, "as")) {
								token = TekToken_as;
							} else if (_TekLexer_compare_consume_lit(lexer, "in")) {
								token = TekToken_in;
							}
							break;
						case 3:
							if (_TekLexer_compare_consume_lit(lexer, "var")) {
								token = TekToken_var;
							} else if (_TekLexer_compare_consume_lit(lexer, "mut")) {
								token = TekToken_mut;
							} else if (_TekLexer_compare_consume_lit(lexer, "mod")) {
								token = TekToken_mod;
							} else if (_TekLexer_compare_consume_lit(lexer, "for")) {
								token = TekToken_for;
							} else if (_TekLexer_compare_consume_lit(lexer, "lib")) {
								token = TekToken_lib;
							}
							break;
						case 4:
							if (_TekLexer_compare_consume_lit(lexer, "else")) {
								token = TekToken_else;
							} else if (_TekLexer_compare_consume_lit(lexer, "proc")) {
								token = TekToken_proc;
							} else if (_TekLexer_compare_consume_lit(lexer, "loop")) {
								token = TekToken_loop;
							} else if (_TekLexer_compare_consume_lit(lexer, "enum")) {
								token = TekToken_enum;
							} else if (_TekLexer_compare_consume_lit(lexer, "goto")) {
								token = TekToken_goto;
							} else if (_TekLexer_compare_consume_lit(lexer, "#abi")) {
								token = TekToken_directive_abi;
							}
							break;
						case 5:
							if (_TekLexer_compare_consume_lit(lexer, "defer")) {
								token = TekToken_defer;
							} else if (_TekLexer_compare_consume_lit(lexer, "match")) {
								token = TekToken_match;
							} else if (_TekLexer_compare_consume_lit(lexer, "union")) {
								token = TekToken_union;
							} else if (_TekLexer_compare_consume_lit(lexer, "alias")) {
								token = TekToken_alias;
							} else if (_TekLexer_compare_consume_lit(lexer, "macro")) {
								token = TekToken_macro;
							} else if (_TekLexer_compare_consume_lit(lexer, "#type")) {
								token = TekToken_directive_type;
							} else if (_TekLexer_compare_consume_lit(lexer, "#expr")) {
								token = TekToken_directive_expr;
							} else if (_TekLexer_compare_consume_lit(lexer, "#stmt")) {
								token = TekToken_directive_stmt;
							}
							break;
						case 6:
							if (_TekLexer_compare_consume_lit(lexer, "return")) {
								token = TekToken_return;
							} else if (_TekLexer_compare_consume_lit(lexer, "struct")) {
								token = TekToken_struct;
							} else if (_TekLexer_compare_consume_lit(lexer, "interf")) {
								token = TekToken_interf;
							} else if (_TekLexer_compare_consume_lit(lexer, "#error")) {
								token = TekToken_directive_error;
							} else if (_TekLexer_compare_consume_lit(lexer, "#flags")) {
								token = TekToken_directive_flags;
							}
							break;
						case 7:
							if (_TekLexer_compare_consume_lit(lexer, "#import")) {
								token = TekToken_directive_import;
							} else if (_TekLexer_compare_consume_lit(lexer, "#inline")) {
								token = TekToken_directive_inline;
							} else if (_TekLexer_compare_consume_lit(lexer, "#extern")) {
								token = TekToken_directive_extern;
							}
							break;
						case 8:
							if (_TekLexer_compare_consume_lit(lexer, "continue")) {
								token = TekToken_continue;
							} else if (_TekLexer_compare_consume_lit(lexer, "#noalias")) {
								token = TekToken_directive_noalias;
							}
							break;
						case 9:
							if (_TekLexer_compare_consume_lit(lexer, "#bitfield")) {
								token = TekToken_directive_bitfield;
							} else if (_TekLexer_compare_consume_lit(lexer, "#distinct")) {
								token = TekToken_directive_distinct;
							} else if (_TekLexer_compare_consume_lit(lexer, "#noreturn")) {
								token = TekToken_directive_noreturn;
							} else if (_TekLexer_compare_consume_lit(lexer, "#volatile")) {
								token = TekToken_directive_volatile;
							}
							break;
						case 10:
							if (_TekLexer_compare_consume_lit(lexer, "#call_conv")) {
								token = TekToken_directive_call_conv;
							} else if (_TekLexer_compare_consume_lit(lexer, "#intrinsic")) {
								token = TekToken_directive_intrinsic;
							}
							break;
						case 12:
							if (_TekLexer_compare_consume_lit(lexer, "#fallthrough")) {
								token = TekToken_directive_fallthrough;
							}
							break;
						case 14:
							if (_TekLexer_compare_consume_lit(lexer, "#compound_type")) {
								token = TekToken_directive_compound_type;
							}
							break;
						default:
							break;
					}

					if (is_directive && token == TekToken_ident) {
						lexer->code_idx += ident_len;
						bail(TekErrorKind_lexer_unrecognised_directive);
					}
				}

				switch (token) {
					case TekToken_ident:
					case TekToken_label: {
						//
						// deduplicate the identifier using the compiler's global string table.
						// this will allow us to only compare an integer to check for string equality.
						TekValue* value = &token_values[file->token_values_count];
						file->token_values_count += 1;
						value->str_id = TekCompiler_strtab_get_or_insert(c, lexer->code + lexer->code_idx, ident_len);
						_TekLexer_advance_column(lexer, ident_len);
						break;
					};
				}
				break;
			};
		}

		if (token >= 33 && token <= 127) { // if is a single character symbol in the ascii table
			_TekLexer_advance_column(lexer, 1);
		}

		TekLexer_token_add(lexer, file, token, code_idx_start, lexer->code_idx, line_start, column_start);
	}

	TekLexer_token_add(lexer, file, TekToken_end_of_file, lexer->code_idx, lexer->code_idx, lexer->line, lexer->column);

	return tek_true;
BAIL_INCORRECT_CLOSE_BRACKET: {}
	TekTokenOpenBracket* open_bracket = &open_brackets[open_brackets_count - 1];
	error.kind = TekErrorKind_lexer_invalid_close_bracket;
	error.args[1].file_id = file_id;
	error.args[1].token_idx = open_bracket->token_idx;
	// fallthrough
BAIL:
	error.args[0].file_id = file_id;
	error.args[0].token_idx = file->tokens_count;

	// add the token that failed to the tokens stack so the reference
	// to the error location in error.args[0] will work.
	TekLexer_token_add(lexer, file, token, code_idx_start, lexer->code_idx, line_start, column_start);

	//
	// add the rest of the new line start indices to line_code_start_indices
	while (_TekLexer_has_code(lexer)) {
		char byte = _TekLexer_peek_byte(lexer);
		if (byte == '\r' || byte == '\n') {
			_TekLexer_advance_line(lexer, file);
		}
		lexer->code_idx += 1;
	}

	// fallthrough
BAIL_PUSH_ERROR:
	*TekCompiler_error_add(c, error.kind) = error;
	return tek_false;
}

