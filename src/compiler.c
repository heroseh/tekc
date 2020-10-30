#include "internal.h"

thread_local TekWorker* tek_thread_worker = NULL;

TekJob* _TekCompiler_job_get(TekCompiler* c, TekJobId job_id) {
	TekPoolId pool_id = (job_id & TekJobId_id_MASK) >> TekJobId_id_SHIFT;
	TekJob* j = TekPool_id_to_ptr(&c->job_sys.pool, pool_id);
	tek_assert(
		j->counter == ((job_id & TekJobId_counter_MASK) >> TekJobId_counter_SHIFT),
		"attempting to get a job with an old identifier"
	);

	return j;
}

TekJob* _TekCompiler_job_next(TekCompiler* c, TekJobType type, TekJobId* job_id_out) {
	//
	// no jobs, so return NULL.
	if (c->job_sys.pool.count == 0) return NULL;

	//
	// check to see if there is any more jobs for this type.
	// the reasoning here is to reuse the TekWorkers instruction cache.
	// and keep giving it the jobs it last worked on.
	//
	// TODO: test this
	// maybe this is not as good as simply just doing the jobs that
	// are depended on the most (the lowest to the highest enum values)
	// so just having the loop below maybe faster, as it could result in less
	// failed jobs that need to be reiterated over again.
	TekJobList* list = &c->job_sys.type_to_job_list_map[type];
	if (list->head) {
		*job_id_out = list->head;
		TekJob* head = _TekCompiler_job_get(c, list->head);
		list->head = head->next;
		return head;
	}

	//
	// now look over every job type list and find a job.
	for (TekJobType type = 0; type < TekJobType_COUNT; type += 1) {
		list = &c->job_sys.type_to_job_list_map[type];
		if (list->head) {
			*job_id_out = list->head;
			TekJob* head = _TekCompiler_job_get(c, list->head);
			list->head = head->next;
			return head;
		}
	}

	return NULL;
}

void _TekCompiler_job_finish(TekCompiler* c, TekJobId job_id, TekBool success_true_fail_false) {
	//
	// get the job regardless of success so we can validate the counter in the job_id.
	TekJob* j = _TekCompiler_job_get(c, job_id);
	if (success_true_fail_false) {
		//
		// success, increment the counter so the next allocation has a different counter.
		// then deallocate the job.
		j->counter += 1;
		if (j->counter == (TekJobId_counter_MASK >> TekJobId_counter_SHIFT)) {
			j->counter = 0;
		}
		TekPoolId pool_id = (job_id & TekJobId_id_MASK) >> TekJobId_id_SHIFT;
		TekPool_dealloc(&c->job_sys.pool, pool_id);
	} else {
		switch (j->type) {
			case TekJobType_file_lex:
			case TekJobType_file_parse:
				atomic_fetch_or(&c->flags, TekCompilerFlags_is_stopping);
				return;
		}

		//
		// failed, increment the fail count and add the job to the failed linked list
		c->job_sys.failed_count += 1;
		TekJobList* list = &c->job_sys.type_to_job_list_map_failed[j->type];
		if (list->tail) {
			TekJob* tail = _TekCompiler_job_get(c, list->tail);
			tail->next = job_id;
		} else {
			list->head = job_id;
		}
		list->tail = job_id;
	}
}

void _TekWorker_init(TekWorker* w) {
	TekArenaAlctor_init(&w->arena_alctor);
	TekAlctor old_alctor = tek_swap(tek_alctor, ((TekAlctor){ .fn = TekArenaAlctor_TekAlctor_fn, .data = &w->arena_alctor }));

	//
	// preallocate memory: lexer
	//
	w->lexer.token_locs = tek_alloc_array(TekTokenLoc, tek_lexer_init_cap_tokens);
	w->lexer.tokens = tek_alloc_array(TekToken, tek_lexer_init_cap_tokens);
	w->lexer.tokens_cap = tek_lexer_init_cap_tokens;
	TekStk_init_with_cap(&w->lexer.token_values, tek_lexer_init_cap_token_values);
	TekStk_init_with_cap(&w->lexer.string_buf, tek_lexer_init_cap_string_buf);
	TekStk_init_with_cap(&w->lexer.str_entries, tek_lexer_init_cap_str_entries);
	TekStk_init_with_cap(&w->lexer.line_code_start_indices, tek_lexer_init_cap_line_code_start_indices);

	tek_swap(tek_alctor, old_alctor);
}

int _TekWorker_main(void* args) {
	TekWorker* w = args;
	TekCompiler* c = w->c;
	atomic_fetch_add(&c->wait.workers_running_count, 1);

	if (w->arena_alctor.metadata_table) {
		TekArenaAlctor_deinit(&w->arena_alctor);
	}

	_TekWorker_init(w);

	tek_thread_worker = w;

	tek_alctor.fn = TekArenaAlctor_TekAlctor_fn;
	tek_alctor.data = &w->arena_alctor;

	//
	// if this is the first worker, setup the first job by creating the first library and file.
	if (w == &c->workers[0]) {
		TekCompiler_lib_create(c, c->compile_args->file_path);
	}

	TekJobType type = 0;
	TekJobId job_id = 0;
	TekBool stalled = tek_false;
	while (!(atomic_load(&c->flags) & TekCompilerFlags_is_stopping)) {
		TekJob* job = _TekCompiler_job_next(c, type, &job_id);
		if (job == NULL) {
			if (!stalled) {
				uint32_t stalled_count = atomic_fetch_add(&c->stalled_workers_count, 1) + 1;
				stalled = tek_true;
				if (stalled_count == c->workers_count) {
					TekCompiler_signal_stop(c);
				}
			}
			// spin lock while we don't have any jobs.
			tek_cpu_relax();
			continue;
		}
		if (stalled) {
			atomic_fetch_sub(&c->stalled_workers_count, 1);
			stalled = tek_false;
		}
		type = job->type;

		TekBool success = tek_false;
		switch (type) {
			case TekJobType_file_lex:
				success = TekLexer_lex(&w->lexer, c, job->file);
				break;
			case TekJobType_file_parse:
			case TekJobType_file_validate:
			default:
				tek_abort("unhandled job type '%u'", type);
		}

		_TekCompiler_job_finish(c, job_id, success);
	}

	tek_thread_worker = NULL;

	TekWorker_terminate(w, 0);
}

TekJob* TekCompiler_job_queue(TekCompiler* c, TekJobType type) {
	//
	// allocate a new job from the pool.
	TekPoolId pool_id = 0;
	TekJob* j = TekPool_alloc(&c->job_sys.pool, &pool_id);

	// backup the counter, if this is the first time, this will be zero
	// as the pool zeros all of its newly allocated memory.
	uint8_t counter = j->counter;

	// zero the job data and initialize it.
	tek_zero_elmt(j);
	j->type = type;
	j->counter = counter;

	//
	// check to see if we have allocated more jobs than the identifier can hold
	pool_id <<= TekJobId_id_SHIFT;
	tek_assert(pool_id <= TekJobId_id_MASK, "the maximum number of jobs has been reached. MAX: %u", TekJobId_id_MASK >> TekJobId_id_SHIFT);

	// now create the id from the pool id and the counter
	TekJobId job_id = (pool_id & TekJobId_id_MASK) |
		((counter << TekJobId_counter_SHIFT) & TekJobId_counter_MASK);

	//
	// store the job at the tail of the linked list for this type of job
	TekJobList* list = &c->job_sys.type_to_job_list_map[j->type];
	if (list->tail) {
		TekJob* tail = _TekCompiler_job_get(c, list->tail);
		tail->next = job_id;
	} else {
		list->head = job_id;
	}
	list->tail = job_id;

	//
	// now return the pointer to the job, so the caller can set up the rest of data
	return j;
}

void TekCompiler_init(TekCompiler* c, uint32_t workers_count) {
	tek_zero_elmt(c);

	c->workers = tek_alloc_array(TekWorker, workers_count);
	tek_zero_elmts(c->workers, workers_count);
	c->workers_count = workers_count;

	c->job_sys.type_to_job_list_map = tek_alloc_array(TekJobList, TekJobType_COUNT);
	tek_zero_elmts(c->job_sys.type_to_job_list_map, TekJobType_COUNT);

	c->job_sys.type_to_job_list_map_failed = tek_alloc_array(TekJobList, TekJobType_COUNT);
	tek_zero_elmts(c->job_sys.type_to_job_list_map_failed, TekJobType_COUNT);
}

void TekCompiler_deinit(TekCompiler* c) {
	tek_abort("unimplemented");
}

TekStrId TekCompiler_strtab_get_or_insert(TekCompiler* c, char* str, uint32_t str_len) {
	TekSpinMtx_lock(&c->lock.strtab);
	TekStrId str_id = TekStrTab_get_or_insert(&c->strtab, str, str_len);
	TekSpinMtx_unlock(&c->lock.strtab);
	return str_id;
}

TekStrEntry TekCompiler_strtab_get_entry(TekCompiler* c, TekStrId str_id) {
	TekSpinMtx_lock(&c->lock.strtab);
	TekStrEntry str_entry = TekStrTab_get_entry(&c->strtab, str_id);
	TekSpinMtx_unlock(&c->lock.strtab);
	return str_entry;
}

TekFile* TekCompiler_file_create(TekCompiler* c, char* file_path, TekStrId parent_file_path_str_id, TekBool is_lib_root) {
	//
	// resolve any symlinks and create an absolute path
	char path[PATH_MAX];
	int res = tek_file_path_normalize_resolve(file_path, path);
	if (res != 0) {
		TekError error = {
			.kind = TekErrorKind_invalid_file_path,
			.args[0] = { .file_path = file_path },
			.args[1] = { .errnum = res },
		};
		TekCompiler_error_add(c, &error);
		TekCompiler_signal_stop(c);
		return NULL;
	}

	//
	// deduplicate the file path
	// strlen + 1 to add the null terminator.
	TekStrId path_str_id = TekCompiler_strtab_get_or_insert(c, path, strlen(path) + 1);

	TekFile* file = NULL;
	TekSpinMtx_lock(&c->lock.files);
	{
		uint32_t id = TekKVStk_find_key_str_id(&c->files, 0, c->files.count, path_str_id);

		// return NULL if the file already exist
		if (id) {
			if (is_lib_root) {
				TekError error = {
					.kind = TekErrorKind_lib_root_file_is_used_in_another_lib,
					.args[0] = { .str_id = path_str_id },
					.args[1] = { .str_id = parent_file_path_str_id },
				};
				TekCompiler_error_add(c, &error);
				TekCompiler_signal_stop(c);
			}
			TekSpinMtx_unlock(&c->lock.files);
			return NULL;
		}

		file = tek_alloc_elmt(TekFile);
		TekKVStk_push(&c->files, &path_str_id, &file);
	}
	TekSpinMtx_unlock(&c->lock.files);

	file->path_str_id = path_str_id;

	TekJob* job = TekCompiler_job_queue(c, TekJobType_file_lex);
	job->file = file;
	return file;
}

void TekCompiler_lib_create(TekCompiler* c, char* root_src_file_path) {
	//
	// allocate a new library, this IS ZEROED from the TekWorker.arena_alctor.
	TekLib* lib = tek_alloc_elmt(TekLib);

	//
	// and the library pointer to the stack in the compiler
	TekSpinMtx_lock(&c->lock.libs);
	*TekStk_push(&c->libs, NULL) = lib;
	TekSpinMtx_unlock(&c->lock.libs);

	//
	// allocate a new file, this IS ZEROED from the TekWorker.arena_alctor.
	TekFile* file = TekCompiler_file_create(c, root_src_file_path, 0, tek_true);
	if (file == NULL) return;

	*TekStk_push(&lib->files, NULL) = file;
}

void TekCompiler_compile_start(TekCompiler* c, TekCompileArgs* args) {
	// unlocked in TekWorker_terminate
	TekMtx_lock(&c->wait.mtx);

	atomic_store(&c->flags, 0);
	c->compile_args = args;

	for (uint32_t i = 0; i < c->workers_count; i += 1) {
		TekWorker* w = &c->workers[i];
		w->c = c;
		if (thrd_create(&w->thread, _TekWorker_main, w) != thrd_success) {
			atomic_fetch_or(&c->flags, TekCompilerFlags_is_stopping);
			break;
		}
	}
}

void TekCompiler_debug_lexer_tokens(TekCompiler* c) {
	char ascii_symbol_buf[2];

	TekStk(char) output = {0};

	for (uint32_t i = 0; i < c->files.count; i += 1) {
		TekFile* file = *TekKVStk_get_value(&c->files, i);

		char* path = TekStrEntry_value(TekStrTab_get_entry(&c->strtab, file->path_str_id));
		TekStk_push_str_fmt(&output, "########## FILE: %s ##########\n", path);

		uint32_t value_idx = 0;
		for (uint32_t j = 0; j < file->tokens_count; j += 1) {
			TekValue* value = &file->token_values[value_idx];
			TekToken token = file->tokens[j];

			char* token_name = NULL;
			if (token < TekToken_ident) { // is ascii symbol
				ascii_symbol_buf[0] = token;
				ascii_symbol_buf[1] = '\0';
				token_name = ascii_symbol_buf;
			} else {
				token_name = TekToken_strings_non_ascii[token];
			}

			switch (token) {
				case TekToken_lit_bool:
					TekStk_push_str_fmt(&output, "%s -> %s\n", token_name, value->bool_ ? "true" : "false");
					value_idx += 1;
					break;
				case TekToken_lit_uint:
					TekStk_push_str_fmt(&output, "%s -> %zu\n", token_name, (uint64_t)value->uint);
					value_idx += 1;
					break;
				case TekToken_lit_sint:
					TekStk_push_str_fmt(&output, "%s -> %zd\n", token_name, (int64_t)value->sint);
					value_idx += 1;
					break;
				case TekToken_lit_float:
					TekStk_push_str_fmt(&output, "%s -> %f\n", token_name, (double)value->float_);
					value_idx += 1;
					break;

				case TekToken_ident:
				case TekToken_ident_abstract:
				case TekToken_label:
				case TekToken_lit_string: {
					if (value->str_id) {
						TekStrEntry entry = TekStrTab_get_entry(&c->strtab, value->str_id);
						uint32_t str_len = TekStrEntry_len(entry);
						char* str = TekStrEntry_value(entry);
						TekStk_push_str_fmt(&output, "%s -> \"", token_name);
						uint32_t old_count = output.count;
						TekStk_push_many(&output, str, str_len);
						TekStk_push_str(&output, "\"\n");
					} else {
						TekStk_push_str_fmt(&output, "%s -> \"\"\n", token_name);
					}
					value_idx += 1;
					break;
				};
				case '\n':
					TekStk_push_str(&output, "\n");
					break;
				default:
					TekStk_push_str_fmt(&output, "%s\n", token_name);
					break;
			}
		}
	}

	int res = tek_file_write(tek_lexer_debug_token_path, output.TekStk_data, output.count);
	tek_assert(res == 0, "failed to write token file: %s", strerror(res));
	TekStk_deinit(&output);
}

void TekCompiler_compile_wait(TekCompiler* c) {
	TekMtx_lock(&c->wait.mtx);
	TekMtx_unlock(&c->wait.mtx);

	if (!TekCompiler_has_errors(c)) {
#if TEK_LEXER_DEBUG_TOKEN
		TekCompiler_debug_lexer_tokens(c);
#endif
	}
}

static char* TekErrorKind_string_fmts[TekErrorKind_COUNT] = {
	[TekErrorKind_invalid_file_path] = "invalid file path at \"%s\" reason \"%s\"",
	[TekErrorKind_lib_root_file_is_used_in_another_lib] = "library root file \"%s\" is used in \"%s\"",
	[TekErrorKind_lexer_file_read_failed] = "failed to read in file at \"%s\" reason \"%s\"",
};

static char* TekErrorKind_message[TekErrorKind_COUNT] = {
	[TekErrorKind_lexer_no_open_brackets_to_close] = "no open brackets to close",
	[TekErrorKind_lexer_binary_integer_can_only_have_zero_and_one] = "binary integer can only have 0s or 1s",
	[TekErrorKind_lexer_octal_integer_has_a_max_digit_of_seven] = "octal integer has a maximum digit of 7",
	[TekErrorKind_lexer_binary_literals_only_allow_for_int] = "binary literals are not allowed for float literals",
	[TekErrorKind_lexer_octal_literals_only_allow_for_int] = "octal literals are not allowed for float literals",
	[TekErrorKind_lexer_hex_literals_only_allow_for_int] = "hex literals are not allowed for float literals",
	[TekErrorKind_lexer_float_has_multiple_decimal_points] = "float cannot have multiple decimal points",
	[TekErrorKind_lexer_expected_int_value_after_radix_prefix] = "an integer value must follow 0x, 0o and 0b radix prefixes",
	[TekErrorKind_lexer_expected_delimiter_after_num] = "expected delimiter after number",
	[TekErrorKind_lexer_overflow_uint] = "overflow of maximum 64-bit unsigned integer",
	[TekErrorKind_lexer_overflow_sint] = "overflow of minimum 64-bit signed integer",
	[TekErrorKind_lexer_overflow_float] = "value cannot be represented in a 64-bit float",
	[TekErrorKind_lexer_unclosed_string_literal] = "unclosed string literal",
	[TekErrorKind_lexer_new_line_in_a_single_line_string] = "new line in a single line string. multiline strings start with a newline",
	[TekErrorKind_lexer_unrecognised_directive] = "unrecognised directive",
	[TekErrorKind_lexer_unsupported_token] = "unsupported token",
	[TekErrorKind_lexer_expected_a_compile_time_token] = "expected a compile time token",
	[TekErrorKind_lexer_unclosed_block_comment] = "unclosed block comment",
	[TekErrorKind_lexer_invalid_string_ascii_esc_char_code_fmt] = "invalid string ascii escape character code format",
	[TekErrorKind_lexer_invalid_string_esc_sequence] = "invalid string escape sequence",
	[TekErrorKind_lexer_invalid_close_bracket] = "invalid close bracket",
	[TekErrorKind_lexer_multiline_string_indent_different_char] = "a different character has been used to indent the multiline string",
	[TekErrorKind_lexer_multiline_string_indent_is_not_enough] = "the indentation does not match the first indentation of the multiline string",
};

static char* TekErrorKind_double_info_lines[TekErrorKind_COUNT][2] = {
	[TekErrorKind_lexer_invalid_close_bracket] = { "incorrect close here", "previously opened bracket here" },
	[TekErrorKind_lexer_multiline_string_indent_different_char] = { "incorrect", "original here" },
	[TekErrorKind_lexer_multiline_string_indent_is_not_enough] = { "incorrect", "original here" },
};

void TekCompiler_error_string_error_line(TekStk(char)* string_out, TekErrorKind kind, TekBool use_ascii_colors) {
	char* fmt = use_ascii_colors
		? "\x1b[1;"tek_error_color_error"merror: "
		  "\x1b["tek_error_color_error_message"m%s\n"
		  "\x1b[0m"
		: "error: %s\n";
	TekStk_push_str_fmt(string_out, fmt, TekErrorKind_message[kind]);
}

void TekCompiler_error_string_info_line(TekStk(char)* string_out, char* info, TekBool use_ascii_colors) {
	char* fmt = use_ascii_colors
		? "\x1b[1;"tek_error_color_info"m%s:\n\x1b[0m"
		: "%s:\n";
	TekStk_push_str_fmt(string_out, fmt, info);
}

void TekCompiler_error_string_code_span(TekStk(char)* string_out, char* code, uint32_t start, uint32_t end) {
	for (uint32_t i = start; i < end; i += 1) {
		char byte = code[i];
		if (byte == '\t') {
			static char* spaces = "    ";
			TekStk_push_str(string_out, spaces);
		} else {
			*TekStk_push(string_out, NULL) = byte;
		}
	}
}

void TekCompiler_error_string_code(TekCompiler* c, TekStk(char)* string_out, TekFile* file, uint32_t line, uint32_t column, uint32_t code_idx_start, uint32_t code_idx_end, TekBool use_ascii_colors) {
	char* code = file->code;
	uint32_t code_idx_prev_line_start = file->line_code_start_indices[line == 1 ? 0 : line - 2];
	uint32_t code_idx_line_start = file->line_code_start_indices[line - 1];
	uint32_t code_idx_next_line_start = file->line_code_start_indices[line >= file->lines_count ? file->lines_count - 1 : line];
	uint32_t code_idx_next_next_line_start = file->line_code_start_indices[line + 1 >= file->lines_count ? file->lines_count - 1 : line + 1];

	char* path = TekStrEntry_value(TekCompiler_strtab_get_entry(c, file->path_str_id));
	char* fmt = use_ascii_colors
		? "\x1b[1;"tek_error_color_file"mfile: "
		  "\x1b[1;"tek_error_color_info"m %s:%u:%u\n"
		  "\x1b[0m"
		: "file: %s:%u:%u\n";
	TekStk_push_str_fmt(string_out, fmt, path, line, column);

	// print the line before and the line the error sits on
	TekCompiler_error_string_code_span(string_out, code, code_idx_prev_line_start, code_idx_next_line_start);

	//
	// print a line that points to where the error is on the previous print out
	{
		// print out the indentation of the line that the error is on.
		// so this is friendly to terminals with different tab indentation levels
		uint32_t i = code_idx_line_start;
		while (i < code_idx_start) {
			char byte = code[i];
			if (byte == '\t') {
				static char* spaces = "    ";
				TekStk_push_str(string_out, spaces);
			} else if (byte == ' ') {
				*TekStk_push(string_out, NULL) = ' ';
			} else {
				break;
			}
			i += 1;
		}

		//
		// print out the spaces to get to where the error is
		while (i < code_idx_start) {
			char byte = code[i];
			if (byte == '\t') {
				static char* spaces = "    ";
				TekStk_push_str(string_out, spaces);
			} else {
				*TekStk_push(string_out, NULL) = ' ';
			}
			i += 1;
		}

		if (use_ascii_colors) {
			TekStk_push_str(string_out, "\x1b["tek_error_color_arrows"m");
		}

		TekBool has_printed_arrow = tek_false;
		//
		// print out the arrows
		while (i < code_idx_end || !has_printed_arrow) {
			char byte = code[i];
			if (byte == '\t') {
				static char* spaces = "^^^^";
				TekStk_push_str(string_out, spaces);
			} else {
				*TekStk_push(string_out, NULL) = '^';
			}
			i += 1;
			has_printed_arrow = tek_true;
		}

		if (use_ascii_colors) {
			TekStk_push_str(string_out, "\x1b[0m");
		}

		*TekStk_push(string_out, NULL) = '\n';
	}

	// print the final line
	TekCompiler_error_string_code_span(string_out, code, code_idx_next_line_start, code_idx_next_next_line_start);
}

void (*TekCompilerErrorStringFn)(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors, TekError* e);

void TekCompiler_error_string_file_path_errno(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors, TekError* e) {
	tek_abort("unimplemented");
}

void TekCompiler_error_string_single(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors, TekError* e) {
	TekFile* file = e->args[0].file;
	TekTokenLoc* loc = &file->token_locs[e->args[0].token_idx];

	TekCompiler_error_string_error_line(string_out, e->kind, use_ascii_colors);
	TekCompiler_error_string_code(c, string_out, file, loc->line, loc->column, loc->code_idx_start, loc->code_idx_end, use_ascii_colors);
}

void TekCompiler_error_string_double(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors, TekError* e) {
	TekCompiler_error_string_error_line(string_out, e->kind, use_ascii_colors);
	char** info_lines = TekErrorKind_double_info_lines[e->kind];

	TekFile* file = e->args[0].file;
	TekTokenLoc* loc = &file->token_locs[e->args[0].token_idx];
	TekCompiler_error_string_info_line(string_out, info_lines[0], use_ascii_colors);
	TekCompiler_error_string_code(c, string_out, file, loc->line, loc->column, loc->code_idx_start, loc->code_idx_end, use_ascii_colors);

	file = e->args[1].file;
	loc = &file->token_locs[e->args[1].token_idx];
	TekCompiler_error_string_info_line(string_out, info_lines[1], use_ascii_colors);
	TekCompiler_error_string_code(c, string_out, file, loc->line, loc->column, loc->code_idx_start, loc->code_idx_end, use_ascii_colors);
}

void TekCompiler_errors_string(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors) {
	for (uint32_t i = 0; i < c->errors.count; i += 1) {
		TekError* e = &c->errors.TekStk_data[i];
		switch (e->kind) {
			case TekErrorKind_invalid_file_path:
			case TekErrorKind_lexer_file_read_failed:
				TekCompiler_error_string_file_path_errno(c, string_out, use_ascii_colors, e);
				break;
			case TekErrorKind_lib_root_file_is_used_in_another_lib:
				break;
			case TekErrorKind_lexer_no_open_brackets_to_close:
			case TekErrorKind_lexer_binary_integer_can_only_have_zero_and_one:
			case TekErrorKind_lexer_octal_integer_has_a_max_digit_of_seven:
			case TekErrorKind_lexer_binary_literals_only_allow_for_int:
			case TekErrorKind_lexer_hex_literals_only_allow_for_int:
			case TekErrorKind_lexer_float_has_multiple_decimal_points:
			case TekErrorKind_lexer_expected_int_value_after_radix_prefix:
			case TekErrorKind_lexer_expected_delimiter_after_num:
			case TekErrorKind_lexer_overflow_uint:
			case TekErrorKind_lexer_overflow_sint:
			case TekErrorKind_lexer_overflow_float:
			case TekErrorKind_lexer_unclosed_string_literal:
			case TekErrorKind_lexer_new_line_in_a_single_line_string:
			case TekErrorKind_lexer_unrecognised_directive:
			case TekErrorKind_lexer_unsupported_token:
			case TekErrorKind_lexer_expected_a_compile_time_token:
			case TekErrorKind_lexer_unclosed_block_comment:
			case TekErrorKind_lexer_invalid_string_ascii_esc_char_code_fmt:
			case TekErrorKind_lexer_invalid_string_esc_sequence:
				TekCompiler_error_string_single(c, string_out, use_ascii_colors, e);
				break;
			case TekErrorKind_lexer_invalid_close_bracket:
			case TekErrorKind_lexer_multiline_string_indent_different_char:
			case TekErrorKind_lexer_multiline_string_indent_is_not_enough:
				TekCompiler_error_string_double(c, string_out, use_ascii_colors, e);
				break;
			default:
				tek_abort("unhandled error kind %u", e->kind);
		}
	}
}

void TekCompiler_error_add(TekCompiler* c, TekError* error) {
	TekSpinMtx_lock(&c->lock.errors);
	TekStk_push(&c->errors, error);
	TekSpinMtx_unlock(&c->lock.errors);
}

void TekCompiler_signal_stop(TekCompiler* c) {
	// set this flag so all workers will stop working
	atomic_fetch_or(&c->flags, TekCompilerFlags_is_stopping);
}

TekBool TekCompiler_has_errors(TekCompiler* c) {
	if (atomic_load(&c->flags) & TekCompilerFlags_out_of_memory)
		return tek_true;

	TekSpinMtx_lock(&c->lock.errors);
	TekBool has_errors = c->errors.count;
	TekSpinMtx_unlock(&c->lock.errors);
	return has_errors;
}

noreturn void TekWorker_terminate(TekWorker* w, int exit_code) {
	uint16_t prev_count = atomic_fetch_sub(&w->c->wait.workers_running_count, 1);
	if (prev_count == 1) {
		// this is the last worker to terminate. so unlock the mutex.
		TekMtx_unlock(&w->c->wait.mtx);
	}
	// terminate the current thread.
	thrd_exit(exit_code);
}

noreturn void TekWorker_terminate_out_of_memory(TekWorker* w) {
	// set these flags so the other workers will stop working
	// and the user can check for an out of memory error.
	atomic_fetch_or(&w->c->flags, TekCompilerFlags_out_of_memory | TekCompilerFlags_is_stopping);

	TekWorker_terminate(w, 1);
}

