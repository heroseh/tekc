#include "internal.h"

TekJob* _TekCompiler_job_get(TekCompiler* c, TekJobId job_id) {
	tek_assert(job_id, "cannot get a job with a NULL identifier");
	TekJob* jobs = TekCompiler_jobs(c);
	uint32_t job_idx = ((job_id & TekJobId_id_MASK) >> TekJobId_id_SHIFT) - 1;
	TekJob* j = &jobs[job_idx];
	tek_assert(
		j->counter == (job_id & TekJobId_counter_MASK) >> TekJobId_counter_SHIFT,
		"attempting to get a job with an identifier has been freed");
	return j;
}

TekJobId _TekCompiler_job_list_take(TekCompiler* c, TekJobList* list) {
	TekSpinMtx_lock(&list->mtx);

	TekJobId head_id = list->head;
	if (head_id) {
		//
		// take from the list head by setting the next job as the head job
		TekJob* head = _TekCompiler_job_get(c, head_id);
		list->head = head->next;

		//
		// set the tail to null if we have reached the end of the list
		if (list->tail == head_id) {
			list->tail = 0;
		}
	}

	TekSpinMtx_unlock(&list->mtx);
	return head_id;
}

void _TekCompiler_job_list_add(TekCompiler* c, TekJobList* list, TekJobId id) {
	TekSpinMtx_lock(&list->mtx);

	if (list->tail) {
		TekJob* tail = _TekCompiler_job_get(c, list->tail);
		tail->next = id;
	} else {
		list->head = id;
	}
	list->tail = id;

	TekSpinMtx_unlock(&list->mtx);
}

TekJobId _TekCompiler_job_next(TekCompiler* c, TekJobType type) {
	//
	// spin around in here and wait the a job to become available
	uint32_t stalled_count = atomic_fetch_add(&c->stalled_workers_count, 1) + 1;
	uint32_t count = atomic_load(&c->job_sys.available_count);
	while (1) {
		if (count == 0) {
			if (stalled_count == c->workers_count) {
				//
				// this is the last worker to stall.
				// rerun the failed jobs if some have succeed since the last time all the workers stalled.
				// otherwise stop the compiler
				uint32_t failed_count = atomic_load(&c->job_sys.failed_count);
				if (failed_count == 0 || failed_count == c->job_sys.failed_count_last_iteration) {
					atomic_fetch_or(&c->flags, TekCompilerFlags_is_stopping);
					return 0;
				}

				//
				// some jobs succeed this time around.
				// so copy the failed lists to the available lists.
				tek_copy_elmts(c->job_sys.type_to_job_list_map, c->job_sys.type_to_job_list_map_failed, TekJobType_COUNT);

				// then zero the failed lists
				tek_zero_elmts(c->job_sys.type_to_job_list_map_failed, TekJobType_COUNT);

				// finally increment the available_count
				c->job_sys.failed_count_last_iteration = failed_count;
				atomic_store(&c->job_sys.available_count, failed_count);
				count = failed_count;
				continue;
			}

			//
			// if the compiler is stopping then return.
			if (atomic_load(&c->flags) & TekCompilerFlags_is_stopping)
				return 0;

			tek_cpu_relax();
			count = atomic_load(&c->job_sys.available_count);
			continue;
		}

		//
		// try to substract 1. if we are thread to do so.
		// then break and collect the job.
		if (atomic_compare_exchange_weak(&c->job_sys.available_count, &count, count - 1))
			break;
	}

	atomic_fetch_sub(&c->stalled_workers_count, 1);

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
	TekJobId id = _TekCompiler_job_list_take(c, list);
	if (id) return id;

	//
	// now look over every job type list and find a job.
	for (TekJobType type = 0; type < TekJobType_COUNT; type += 1) {
		list = &c->job_sys.type_to_job_list_map[type];
		TekJobId id = _TekCompiler_job_list_take(c, list);
		if (id) return id;
	}

	tek_abort("it should be impossible to not find a job in one of the lists");
}

void _TekCompiler_job_finish(TekCompiler* c, TekJobId job_id, TekBool success_true_fail_false) {
	//
	// get the job regardless of success so we can validate the counter in the job_id.
	TekJob* j = _TekCompiler_job_get(c, job_id);
	if (success_true_fail_false) {
		//
		// success, increment the counter so the next allocation has a different counter.
		// then deallocate the job.
		if (j->counter == (TekJobId_counter_MASK >> TekJobId_counter_SHIFT)) {
			j->counter = 0;
		} else {
			j->counter += 1;
		}

		// clear the counter
		job_id &= ~TekJobId_counter_MASK;
		// now set the new counter value in the identifier
		job_id |= (j->counter << TekJobId_counter_SHIFT) & TekJobId_counter_MASK;
		_TekCompiler_job_list_add(c, &c->job_sys.free_list, job_id);
	} else {
		//
		// if this is a critical fail, then stop the compilation.
		switch (j->type) {
			case TekJobType_lex_file:
			case TekJobType_gen_syn_file:
				TekCompiler_signal_stop(c);
				return;
		}

		//
		// failed, increment the fail count and add the job to the failed linked list for it's type
		atomic_fetch_add(&c->job_sys.failed_count, 1);
		TekJobList* list = &c->job_sys.type_to_job_list_map_failed[j->type];
		_TekCompiler_job_list_add(c, list, job_id);
	}
}

int _TekWorker_main(void* args) {
	TekWorker* w = args;
	TekCompiler* c = w->c;

	//
	// wait for all the workers to enter this function.
	// if this is the last worker to enter, then free all the workers from the spin lock.
	uint32_t w_idx = atomic_fetch_add(&c->running_workers_count, 1);
	if (w_idx + 1 == c->workers_count) {
		atomic_fetch_and(&c->flags, ~TekCompilerFlags_starting_up);
	} else {
		while (atomic_load(&c->flags) & TekCompilerFlags_starting_up) {
			tek_cpu_relax();
		}
	}

	//
	// if this is the first worker, setup the first job by creating the first library and file.
	if (w == TekCompiler_workers(c)) {
		TekCompiler_lib_create(c, c->compile_args->file_path);
	}

	TekJobType type = 0;
	TekJobId job_id = 0;
	while (!(atomic_load(&c->flags) & TekCompilerFlags_is_stopping)) {
		job_id = _TekCompiler_job_next(c, type);
		if (job_id == 0)
			break;

		TekJob* job = _TekCompiler_job_get(c, job_id);
		type = job->type;

		TekBool success = tek_false;
		switch (type) {
			case TekJobType_lex_file:
				success = TekLexer_lex(&w->lexer, c, job->file_id);
				break;
			case TekJobType_gen_syn_file:
				success = TekGenSyn_gen_file(w, job->file_id);
				break;
			case TekJobType_gen_sem_file:
			default:
				tek_abort("unhandled job type '%u'", type);
		}

		_TekCompiler_job_finish(c, job_id, success);
	}

	if (atomic_fetch_sub(&c->running_workers_count, 1) == 1) {
		//
		// unlock the mutex for the TekCompiler_compile_wait function.
		TekMtx_unlock(&c->wait_mtx);
		atomic_fetch_and(&c->flags, ~TekCompilerFlags_is_running);
	}

	return 0;
}

TekVirtMemError tek_mem_segs_reserve(uint8_t memsegs_count, uintptr_t* memsegs_sizes, void** segments_out) {
	uintptr_t page_size = tek_virt_mem_page_size();

	uintptr_t total_size = 0;
	for (uint32_t i = 0; i < memsegs_count; i += 1) {
		uintptr_t size = memsegs_sizes[i];
		tek_assert(
			size > page_size && size % page_size == 0,
			"segment index '%u' with size of '0x%x(%zu)' must be greater than and a multiple of the page size '%u'",
			i, size, size, page_size);
		total_size += size;
	}

	// add the size for all of the guard pages that sit directly after.
	total_size += memsegs_count * page_size;

	total_size = (uintptr_t)tek_ptr_round_up_align((void*)total_size, page_size);

	//
	// reserve memory for all the memory all at once
	void* mem = tek_virt_mem_reserve(NULL, total_size, TekVirtMemProtection_read_write);
	if (mem == NULL) {
		return tek_virt_mem_get_last_error();
	}

	//
	// store the segments pointers in segments_out and mark the guard page.
	for (uintptr_t i = 0; i < memsegs_count; i += 1) {
		segments_out[i] = mem;
		mem = tek_ptr_add(mem, memsegs_sizes[i]);

		// mark the guard page after the group as no access.
		if (!tek_virt_mem_protection_set(mem, page_size, TekVirtMemProtection_no_access)) {
			return tek_virt_mem_get_last_error();
		}
		mem = tek_ptr_add(mem, page_size);
	}

	return 0;
}

TekVirtMemError tek_mem_segs_reset(uint8_t memsegs_count, uintptr_t* memsegs_sizes, void** segments) {
	//
	// decommit all the segments so the memory can return to the OS and it'll all be zeroed apon next access.
	for (uintptr_t i = 0; i < memsegs_count; i += 1) {
		if (!tek_virt_mem_decommit(segments[i], memsegs_sizes[i])) {
			return tek_virt_mem_get_last_error();
		}
	}

	return 0;
}

TekVirtMemError tek_mem_segs_release(uint8_t memsegs_count, uintptr_t* memsegs_sizes, void** segments_in_out) {
	uintptr_t page_size = tek_virt_mem_page_size();

	//
	// release all the segments
	for (uintptr_t i = 0; i < memsegs_count; i += 1) {
		if (!tek_virt_mem_release(segments_in_out[i], memsegs_sizes[i])) {
			return tek_virt_mem_get_last_error();
		}

		//
		// the guard_page sit right after the segment
		void* guard_page = tek_ptr_add(segments_in_out[i], memsegs_sizes[i]);
		if (!tek_virt_mem_release(guard_page, page_size)) {
			return tek_virt_mem_get_last_error();
		}

		segments_in_out[i] = NULL;
	}

	return 0;
}

TekJob* TekCompiler_job_queue(TekCompiler* c, TekJobType type) {
	//
	// allocate a new job from the pool.
	TekPoolId pool_id = 0;

	TekJobId id = _TekCompiler_job_list_take(c, &c->job_sys.free_list);
	if (id == 0) {
		id = atomic_fetch_add(&c->jobs_count, 1) + 1;
		tek_assert(id <= TekJobId_id_MASK, "the maximum number of jobs has been reached. MAX: %u", TekJobId_id_MASK);
	}

	uint32_t job_idx = ((id & TekJobId_id_MASK) >> TekJobId_id_SHIFT) - 1;
	TekJob* jobs = TekCompiler_jobs(c);
	TekJob* j = &jobs[job_idx];

	// backup the counter, if this is the first time, this will be zero
	// as the memory defaults to zero
	uint8_t counter = j->counter;

	// zero the job data and initialize it.
	tek_zero_elmt(j);
	j->type = type;
	j->counter = counter;


	//
	// store the job at the tail of the linked list for this type of job
	TekJobList* list = &c->job_sys.type_to_job_list_map[j->type];
	_TekCompiler_job_list_add(c, list, id);
	atomic_fetch_add(&c->job_sys.available_count, 1);

	//
	// now return the pointer to the job, so the caller can set up the rest of data
	return j;
}

TekCompiler* TekCompiler_init() {
	void* segments[TekMemSegCompiler_COUNT];
	TekVirtMemError res = tek_mem_segs_reserve(TekMemSegCompiler_COUNT, TekMemSegCompiler_sizes, segments);
	if (res) {
		return NULL;
	}
	TekCompiler* c = segments[0];
	tek_copy_elmts(c->segments, segments, TekMemSegCompiler_COUNT);
	return c;
}

void TekCompiler_deinit(TekCompiler* c) {
	tek_mem_segs_release(TekMemSegCompiler_COUNT, TekMemSegCompiler_sizes, c->segments);
}

TekStrId TekCompiler_strtab_get_or_insert(TekCompiler* c, char* str, uint32_t str_len) {
	TekHash hash = tek_hash_fnv(str, str_len, 0);

	//
	// try to find a string entry with the same hash & value and return that.
	// or create a new string entry.
	_Atomic TekHash* hashes = TekCompiler_strtab_hashes(c);
	_Atomic TekStrEntry* entries = TekCompiler_strtab_entries(c);
	char* strings = TekCompiler_strtab_strings(c);
	uint32_t start_idx = 0;
	uint32_t end_idx = atomic_load(&c->strtab_entries_count);
	TekStrId str_id = 0;
	while (1) {
		uint32_t id = tek_atomic_find_hash(hashes, hash, start_idx, end_idx);
		if (id == 0) {
			// did not find the hash in the array
			// now try to compare and exchange the hash at the end of the array.
			// it will be zero if no other thread added since we looked for our key.
			TekHash expected = 0;
			if (atomic_compare_exchange_weak(&hashes[end_idx], &expected, hash)) {
				str_id = end_idx + 1;
				atomic_fetch_add(&c->strtab_entries_count, 1);

				// add the enough room for the size of the string to sit infront of the string.
				// and make sure the next string entry can be aligned correctly by rounding up.
				uintptr_t rounded_len = (uintptr_t)tek_ptr_round_up_align((void*)((uintptr_t)str_len + sizeof(uint32_t)), alignof(uint32_t));
				TekStrEntry entry = &strings[atomic_fetch_add(&c->strtab_strings_size, rounded_len)];
				*(uint32_t*)entry = str_len;
				tek_copy_bytes(tek_ptr_add(entry, sizeof(uint32_t)), str, str_len);

				// now store the entry in the entries array.
				atomic_store(&entries[str_id - 1], entry);
				return str_id;
			}
		} else {
			//
			// found a string with the same hash.
			// check to see if it is a match.

			//
			// fetch the entry from the entries array.
			// the entry will be NULL if it is still being setup.
			// so spin until it is ready.
			TekStrEntry entry = NULL;
			while (!entry) {
				entry = atomic_load(&entries[id - 1]);
				tek_cpu_relax();
			}

			uint32_t entry_str_len = TekStrEntry_len(entry);
			char* entry_str = TekStrEntry_value(entry);
			if (entry_str_len == str_len && memcmp(entry_str, str, str_len) == 0) {
				// the strings matched so return the identifier
				return id;
			}
		}

		//
		// failed to find a match or set a new string here, so start the loop again.
		// starting from where we got to.
		// and refresh the strtab_entries_count to check the new entries added.
		start_idx = id == 0 ? end_idx : id;
		end_idx = atomic_load(&c->strtab_entries_count);
	}
}

TekStrEntry TekCompiler_strtab_get_entry(TekCompiler* c, TekStrId str_id) {
	tek_assert(str_id, "cannot get a string entry with a null string identifiers");
	_Atomic TekStrEntry* entries = TekCompiler_strtab_entries(c);
	return atomic_load(&entries[str_id - 1]);
}

TekFileId TekCompiler_file_get_or_create(TekCompiler* c, char* file_path, TekFileId parent_file_id) {
	//
	// resolve any symlinks and create an absolute path
	char path[PATH_MAX];
	int res = tek_file_path_normalize_resolve(file_path, path);
	if (res != 0) {
		TekError* e = TekCompiler_error_add(c, TekErrorKind_invalid_file_path);
		e->args[0].file_path = file_path;
		e->args[1].errnum = res;
		TekCompiler_signal_stop(c);
		return 0;
	}

	//
	// deduplicate the file path by using the string table.
	// strlen + 1 to add the null terminator.
	TekStrId path_str_id = TekCompiler_strtab_get_or_insert(c, path, strlen(path) + 1);

	//
	// try to find a file with the same path and return that.
	// or create a new file.
	_Atomic TekStrId* file_paths = TekCompiler_file_paths(c);
	TekFile* files = TekCompiler_files(c);
	uint32_t start_idx = 0;
	uint32_t end_idx = atomic_load(&c->files_count);
	TekFileId file_id = 0;
	while (1) {
		uint32_t id = tek_atomic_find_str_id(file_paths, path_str_id, start_idx, end_idx);
		if (id == 0) {
			// did not find the path in the array
			// now try to compare and exchange the path_str_id at the end of the array.
			// it will be zero if no other thread added since we looked for our key.
			TekStrId expected = 0;
			if (atomic_compare_exchange_weak(&file_paths[end_idx], &expected, path_str_id)) {
				file_id = end_idx + 1;
				atomic_fetch_add(&c->files_count, 1);
				break;
			}

			//
			// failed to set our path here, so start the loop again from the end_idx this time.
			// and refresh the files_count to check the new files added.
			start_idx = end_idx;
			end_idx = atomic_load(&c->files_count);
		} else {
			//
			// found a file with this path so return.
			if (parent_file_id == 0) {
				TekFile* parent_file = TekCompiler_file_get(c, parent_file_id);
				TekError* e = TekCompiler_error_add(c, TekErrorKind_lib_root_file_is_used_in_another_lib);
				e->args[0].str_id = path_str_id;
				e->args[1].str_id = parent_file->path_str_id;
				TekCompiler_signal_stop(c);
			}
			return id;
		}
	}

	//
	// this is a new file so set it up and queue it for lexing
	//
	TekFile* file = &files[file_id - 1];
	TekVirtMemError virt_mem_res = tek_mem_segs_reserve(TekMemSegFile_COUNT, TekMemSegFile_sizes, file->segments);
	if (virt_mem_res) {
		TekError* e = TekCompiler_error_add(c, TekErrorKind_virt_mem);
		e->args[0].virt_mem_error = virt_mem_res;
		return 0;
	}

	//
	// memory map the file into the address space
	file->code = tek_virt_mem_map_file(path, TekVirtMemProtection_read, &file->size, &file->handle);
	if (file->code == NULL) {
		TekError* e = TekCompiler_error_add(c, TekErrorKind_lexer_file_read_failed);
		e->args[0].file_id = file_id;
		e->args[1].virt_mem_error = tek_virt_mem_get_last_error();
		return 0;
	}

	file->id = file_id;
	file->path_str_id = path_str_id;

	TekJob* job = TekCompiler_job_queue(c, TekJobType_lex_file);
	job->file_id = file_id;
	return file_id;
}

TekFile* TekCompiler_file_get(TekCompiler* c, TekFileId file_id) {
	tek_assert(file_id, "cannot get a file with a NULL id");
	TekFile* files = TekCompiler_files(c);
	return &files[file_id - 1];
}

TekLibId TekCompiler_lib_create(TekCompiler* c, char* root_src_file_path) {
	//
	// allocate a new library
	TekLib* libs = TekCompiler_libs(c);
	uint32_t idx = atomic_fetch_add(&c->libs_count, 1);
	TekLib* lib = &libs[idx];
	lib->id = idx + 1;

	//
	// allocate the memory segments for the lib structure
	TekVirtMemError res = tek_mem_segs_reserve(TekMemSegLib_COUNT, TekMemSegLib_sizes, lib->segments);
	if (res) {
		TekError* e = TekCompiler_error_add(c, TekErrorKind_virt_mem);
		e->args[0].virt_mem_error = res;
		return 0;
	}

	//
	// create a file for the library's root source file
	TekFileId file_id = TekCompiler_file_get_or_create(c, root_src_file_path, 0);
	if (file_id == 0) return 0;

	//
	// add the file to the lib's file array
	TekFileId* files = TekLib_files(lib);
	files[atomic_fetch_add(&lib->files_count, 1)] = file_id;
	return lib->id;
}

TekLib* TekCompiler_lib_get(TekCompiler* c, TekLibId lib_id) {
	tek_assert(lib_id, "cannot get a lib with a NULL id");
	TekLib* libs = TekCompiler_libs(c);
	return &libs[lib_id - 1];
}

TekCompilerError TekCompiler_compile_start(TekCompiler* c, uint16_t workers_count, TekCompileArgs* args) {
	TekCompilerFlags prev_state_of_flags = atomic_fetch_or(&c->flags, TekCompilerFlags_is_running);
	if (prev_state_of_flags & TekCompilerFlags_is_running) {
		return TekCompilerError_already_running;
	}

	//
	// release all of the memory segments of the libraries and files
	//
	{
		TekLib* libs = TekCompiler_libs(c);
		uint32_t libs_count = atomic_load(&c->libs_count);
		for (uint32_t i = 0; i < libs_count; i += 1) {
			if (libs->segments[0] == NULL) break;
			tek_mem_segs_release(TekMemSegLib_COUNT, TekMemSegLib_sizes, libs->segments);
		}

		TekFile* files = TekCompiler_files(c);
		uint32_t files_count = atomic_load(&c->files_count);
		for (uint32_t i = 0; i < files_count; i += 1) {
			if (files->segments[0] == NULL) break;
			tek_mem_segs_release(TekMemSegFile_COUNT, TekMemSegFile_sizes, files->segments);
		}
	}

	//
	// copy out the segment pointers and then zero the compiler segments.
	void* segments[TekMemSegCompiler_COUNT];
	tek_copy_elmts(segments, c->segments, TekMemSegCompiler_COUNT);
	tek_mem_segs_reset(TekMemSegCompiler_COUNT, TekMemSegCompiler_sizes, segments);

	//
	// copy the segment pointers back and initialize the data.
	tek_copy_elmts(c->segments, segments, TekMemSegCompiler_COUNT);
	atomic_store(&c->flags, TekCompilerFlags_is_running | TekCompilerFlags_starting_up);
	c->compile_args = args;
	c->workers_count = workers_count;

	// the last worker will unlock this mutex at the end of _TekWorker_main
	TekMtx_lock(&c->wait_mtx);

	TekWorker* workers = TekCompiler_workers(c);
	for (uint32_t i = 0; i < workers_count; i += 1) {
		TekWorker* w = &workers[i];
		w->c = c;
		if (thrd_create(&w->thread, _TekWorker_main, w) != thrd_success) {
			atomic_fetch_or(&c->flags, TekCompilerFlags_is_stopping);
			return TekCompilerError_failed_to_start_worker_threads;
		}
	}

	return TekCompilerError_none;
}

void TekCompiler_debug_tokens(TekCompiler* c) {
	char token_name[128];

	TekStk(char) output = {0};

	TekFile* files = TekCompiler_files(c);
	uint32_t files_count = atomic_load(&c->files_count);
	for (uint32_t i = 0; i < files_count; i += 1) {
		TekFile* file = &files[i];
		TekValue* token_values = TekFile_token_values(file);
		TekToken* tokens = TekFile_tokens(file);

		char* path = TekStrEntry_value(TekCompiler_strtab_get_entry(c, file->path_str_id));
		TekStk_push_str_fmt(&output, "########## FILE: %s ##########\n", path);

		uint32_t value_idx = 0;
		for (uint32_t j = 0; j < file->tokens_count; j += 1) {
			TekValue* value = &token_values[value_idx];
			TekToken token = tokens[j];
			TekToken_as_string(token, token_name, sizeof(token_name));

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
						TekStrEntry entry = TekCompiler_strtab_get_entry(c, value->str_id);
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

	int res = tek_file_write(tek_debug_tokens_path, output.TekStk_data, output.count);
	tek_assert(res == 0, "failed to write token file: %s", strerror(res));
	TekStk_deinit(&output);
}

static void TekCompiler_debug_indent(TekStk(char)* output, uint32_t indent_level) {
	static char tabs[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	TekStk_push_str_fmt(output, "%.*s", indent_level, tabs);
}

void TekCompiler_debug_syntax_node(TekCompiler* c, TekFile* file, TekStk(char)* output, TekSynNode* node, uint32_t indent_level);

void TekCompiler_debug_syntax_node_child_abs(TekCompiler* c, TekFile* file, TekStk(char)* output, TekSynNode* node, uint32_t indent_level, char* name, uintptr_t idx) {
	TekCompiler_debug_indent(output, indent_level);
	if (idx) {
		TekStk_push_str_fmt(output, "%s: {\n", name);
		TekSynNode* nodes = TekFile_syntax_tree_nodes(file);
		TekCompiler_debug_syntax_node(c, file, output, &nodes[idx], indent_level + 1);
		TekCompiler_debug_indent(output, indent_level);
		TekStk_push_str(output, "}\n");
	} else {
		TekStk_push_str_fmt(output, "%s: NULL\n", name);
	}
}

void TekCompiler_debug_syntax_node_child(TekCompiler* c, TekFile* file, TekStk(char)* output, TekSynNode* node, uint32_t indent_level, char* name, int64_t rel_idx) {
	TekCompiler_debug_indent(output, indent_level);
	if (rel_idx) {
		TekStk_push_str_fmt(output, "%s: {\n", name);
		TekCompiler_debug_syntax_node(c, file, output, &node[rel_idx], indent_level + 1);
		TekCompiler_debug_indent(output, indent_level);
		TekStk_push_str(output, "}\n");
	} else {
		TekStk_push_str_fmt(output, "%s: NULL\n", name);
	}
}

void TekCompiler_debug_syntax_node_child_has_list_header(TekCompiler* c, TekFile* file, TekStk(char)* output, TekSynNode* node, uint32_t indent_level, char* name, int64_t rel_idx) {
	TekCompiler_debug_indent(output, indent_level);
	if (rel_idx) {
		TekStk_push_str_fmt(output, "%s: {\n", name);
		TekSynNode* nodes = TekFile_syntax_tree_nodes(file);
		uint32_t idx = (node - nodes) + rel_idx;
		while (idx) {
			node = &nodes[idx];
			TekCompiler_debug_indent(output, indent_level + 1);
			TekStk_push_str_fmt(output, "#SynNode #%u list_header\n", idx - 1);
			TekCompiler_debug_syntax_node(c, file, output, node, indent_level + 1);
			idx = node[-1].next_node_idx;
			if (idx)
				TekStk_push_str(output, "\n");
		}
		TekCompiler_debug_indent(output, indent_level);
		TekStk_push_str(output, "}\n");
	} else {
		TekStk_push_str_fmt(output, "%s: NULL\n", name);
	}
}

void TekCompiler_debug_syntax_node(TekCompiler* c, TekFile* file, TekStk(char)* output, TekSynNode* node, uint32_t indent_level) {
NEXT_NODE: {}
	uint32_t ast_idx = node - TekFile_syntax_tree_nodes(file);
	TekCompiler_debug_indent(output, indent_level);
	TekStk_push_str_fmt(output, "#SynNode #%u header (%s)\n", ast_idx, TekSynNodeKind_strings[node->header.kind]);

	switch (node->header.kind) {
		case TekSynNodeKind_expr_list_header:
		case TekSynNodeKind_stmt_list_header:
		case TekSynNodeKind_stmt_fallthrough:
			break;
		default:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "#SynNode #%u payload\n", ast_idx + 1);
			break;
	}

	if (node->header.kind != TekSynNodeKind_expr_list_header && node->header.kind != TekSynNodeKind_stmt_list_header) {
		char token_name[128];
		TekToken token = TekFile_tokens(file)[node->header.token_idx];
		TekToken_as_string(token, token_name, sizeof(token_name));
		TekCompiler_debug_indent(output, indent_level);
		TekStk_push_str_fmt(output, "token: %s\n", token_name);
	}

	switch (node->header.kind) {
		case TekSynNodeKind_anon_struct_ident:
		case TekSynNodeKind_lib_ref:
		case TekSynNodeKind_lib_extern:
		case TekSynNodeKind_macro:
		case TekSynNodeKind_interf:
		case TekSynNodeKind_alias:
		case TekSynNodeKind_type_enum:
		case TekSynNodeKind_enum_field:
		case TekSynNodeKind_expr_compile_time:
			tek_abort("unimplemented syntax node debugging");

		case TekSynNodeKind_type_implicit:
		case TekSynNodeKind_stmt_fallthrough:
		case TekSynNodeKind_expr_match_else:
		case TekSynNodeKind_expr_root_mod:
			break;

		case TekSynNodeKind_ident:
		case TekSynNodeKind_ident_abstract:
		case TekSynNodeKind_label:
		case TekSynNodeKind_expr_lit_string:
		{
			TekStrEntry entry = TekCompiler_strtab_get_entry(c, node[1].ident_str_id);
			char* ident = TekStrEntry_value(entry);
			uint32_t ident_len = TekStrEntry_len(entry);
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "value: %.*s\n", ident_len, ident);
			break;
		};

		case TekSynNodeKind_decl:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "ident", node[1].decl.ident_rel_idx);
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "item", node[1].decl.item_rel_idx);
			break;

		case TekSynNodeKind_mod:
			TekCompiler_debug_syntax_node_child_has_list_header(
				c, file, output, node, indent_level, "entries", node[1].mod.entries_list_head_rel_idx);
			break;

		case TekSynNodeKind_type_proc:
		case TekSynNodeKind_proc:
			if (node[1].proc.is_generic) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str(output, "generic: true\n");
			}
			if (node[1].proc.is_noreturn) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str(output, "noreturn: true\n");
			}
			if (node[1].proc.is_intrinsic) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str(output, "intrinsic: true\n");
			}

			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "call_conv: %s\n", TekProcCallConv_strings[node[1].proc.call_conv]);

			TekCompiler_debug_syntax_node_child_has_list_header(
				c, file, output, node, indent_level, "params", node[1].proc.params_list_head_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "stmt_block", node[1].proc.stmt_block_rel_idx);
			break;

		case TekSynNodeKind_proc_param:
			if (node[1].proc_param.is_vararg) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str(output, "vararg: true\n");
			}

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "ident", node[1].proc_param.ident_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "type", node[1].proc_param.type_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "default_value_expr", node[1].proc_param.default_value_expr_rel_idx);
			break;

		case TekSynNodeKind_var:
			if (node[1].var.is_global) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "global: true\n");
			}
			if (node[1].var.is_static) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "static: true\n");
			}
			if (node[1].var.is_intrinsic) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "intrinsic: true\n");
			}

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "types", node[1].var.types_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "init_exprs", node[1].var.init_exprs_rel_idx);
			break;

		case TekSynNodeKind_import:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "import", node[1].import.expr_rel_idx);
			break;

		case TekSynNodeKind_import_file: {
			TekFile* target_file = TekCompiler_file_get(c, node[1].import_file.file_id);
			char* file_path = TekStrEntry_value(TekCompiler_strtab_get_entry(c, target_file->path_str_id));

			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "file_path: %s\n", file_path);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "ident", node[1].import_file.ident_rel_idx);
			break;
		};

		case TekSynNodeKind_type_struct:
			if (node[1].proc.is_generic) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str(output, "generic: true\n");
			}

			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "abi: %s\n", TekProcCallConv_strings[node[1].type_struct.abi]);

			TekCompiler_debug_syntax_node_child_has_list_header(
				c, file, output, node, indent_level, "fields", node[1].type_struct.fields_list_head_rel_idx);
			break;

		case TekSynNodeKind_struct_field:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "ident", node[1].struct_field.ident_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "type", node[1].struct_field.type_rel_idx);
			break;

		case TekSynNodeKind_type_bounded_int:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "is_signed: %s\n", node[1].type_bounded_int.is_signed ? "true" : "false");

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "bit_count_expr", node[1].type_bounded_int.bit_count_expr_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "range_expr", node[1].type_bounded_int.range_expr_rel_idx);
			break;

		case TekSynNodeKind_type_ptr:
		case TekSynNodeKind_type_ref:
		case TekSynNodeKind_type_view:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "rel_type", node[1].type_ptr.rel_type_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "elmt_type", node[1].type_ptr.elmt_type_rel_idx);
			break;

		case TekSynNodeKind_type_array:
		case TekSynNodeKind_type_stack:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "count", node[1].type_array.count_expr_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "elmt_type", node[1].type_array.elmt_type_rel_idx);
			break;

		case TekSynNodeKind_type_range:
			TekCompiler_debug_syntax_node_child_abs(
				c, file, output, node, indent_level, "elmt_type", node[1].next_node_idx);
			break;

		case TekSynNodeKind_type_qualifier:
			if (node[1].type_qual.flags & TekTypeQualifierFlags_mut) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "mut: true\n");
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "mut_rel_token_idx: %u\n", node[1].type_qual.rel_token_idx_mut);
			}
			if (node[1].type_qual.flags & TekTypeQualifierFlags_noalias) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "noalias: true\n");
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "noalias_rel_token_idx: %u\n", node[1].type_qual.rel_token_idx_noalias);
			}
			if (node[1].type_qual.flags & TekTypeQualifierFlags_volatile) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "volatile: true\n");
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "volatile_rel_token_idx: %u\n", node[1].type_qual.rel_token_idx_volatile);
			}

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "type", node[1].type_qual.type_rel_idx);
			break;

		case TekSynNodeKind_stmt_assign:
		case TekSynNodeKind_expr_op_binary:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "binary_op: %s\n", TekBinaryOp_strings[node[1].binary.op]);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "left", node[1].binary.left_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "right", node[1].binary.right_rel_idx);
			break;

		case TekSynNodeKind_stmt_return: {
			if (node[1].stmt_return.has_label) {
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "#SynNode #%u payload (label)\n", ast_idx + 2);

				TekStrEntry entry = TekCompiler_strtab_get_entry(c, node[2].label_str_id);
				char* label = TekStrEntry_value(entry);
				uint32_t label_len = TekStrEntry_len(entry);
				TekCompiler_debug_indent(output, indent_level);
				TekStk_push_str_fmt(output, "label: %.*s\n", label_len, label);
			}

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "args_list_head_expr", node[1].stmt_return.args_list_head_expr_rel_idx);
			break;
		};

		case TekSynNodeKind_stmt_continue: {
			TekStrEntry entry = TekCompiler_strtab_get_entry(c, node[1].label_str_id);
			char* label = TekStrEntry_value(entry);
			uint32_t label_len = TekStrEntry_len(entry);
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "label: %.*s\n", label_len, label);
			break;
		};

		case TekSynNodeKind_stmt_goto:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "target", node[1].stmt_goto.expr_rel_idx);
			break;

		case TekSynNodeKind_stmt_defer:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "defer", node[1].stmt_defer.expr_rel_idx);
			break;

		case TekSynNodeKind_expr_multi:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "count: %u\n", node[1].expr_multi.count);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "list", node[1].expr_multi.list_head_rel_idx);
			break;

		case TekSynNodeKind_expr_op_unary:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "unary_op: %s\n", TekUnaryOp_strings[node[1].unary.op]);
			break;

		case TekSynNodeKind_expr_if:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "#SynNode #%u payload (else)\n", ast_idx + 2);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "cond_expr", node[1].expr_if.cond_expr_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "success_expr", node[1].expr_if.success_expr_rel_idx);

			TekCompiler_debug_syntax_node_child_abs(
				c, file, output, node, indent_level, "else", node[2].next_node_idx);
			break;

		case TekSynNodeKind_expr_match:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "cond_expr", node[1].expr_match.cond_expr_rel_idx);

			TekCompiler_debug_syntax_node_child_has_list_header(
				c, file, output, node, indent_level, "cases", node[1].expr_match.cases_list_head_rel_idx);
			break;

		case TekSynNodeKind_expr_match_case:
			TekCompiler_debug_syntax_node_child_abs(
				c, file, output, node, indent_level, "cond", node[1].next_node_idx);
			break;

		case TekSynNodeKind_expr_for:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "#SynNode #%u payload (stmt_block)\n", ast_idx + 2);

			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "is_by_value: %s\n", node[1].expr_for.is_by_value ? "true" : "false");

			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "is_reverse: %s\n", node[1].expr_for.is_reverse ? "true" : "false");

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "idents", node[1].expr_for.identifiers_list_head_expr_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "types", node[1].expr_for.types_list_head_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "iter_expr", node[1].expr_for.iter_expr_rel_idx);

			TekCompiler_debug_syntax_node_child_abs(
				c, file, output, node, indent_level, "stmt_block", node[2].next_node_idx);
			break;

		case TekSynNodeKind_expr_named_arg:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "ident", node[1].expr_named_arg.ident_rel_idx);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "value", node[1].expr_named_arg.value_rel_idx);
			break;

		case TekSynNodeKind_expr_stmt_block:
		case TekSynNodeKind_expr_loop:
			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "stmts", node[1].expr_stmt_block.stmts_list_head_rel_idx);
			break;

		case TekSynNodeKind_expr_vararg_spread:
			TekCompiler_debug_syntax_node_child_abs(
				c, file, output, node, indent_level, "expr", node[1].next_node_idx);
			break;

		case TekSynNodeKind_expr_lit_uint:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "value: %zu\n", TekFile_token_values(file)[node[1].token_value_idx].uint);
			break;

		case TekSynNodeKind_expr_lit_sint:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "value: %zd\n", TekFile_token_values(file)[node[1].token_value_idx].sint);
			break;

		case TekSynNodeKind_expr_lit_float:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "value: %f\n", TekFile_token_values(file)[node[1].token_value_idx].float_);
			break;

		case TekSynNodeKind_expr_lit_bool:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "value: %s\n", TekFile_token_values(file)[node[1].token_value_idx].bool_ ? "true" : "false");
			break;

		case TekSynNodeKind_expr_lit_array:
			TekCompiler_debug_syntax_node_child_abs(
				c, file, output, node, indent_level, "elmts", node[1].next_node_idx);
			break;

		case TekSynNodeKind_expr_up_parent_mods:
			TekCompiler_debug_indent(output, indent_level);
			TekStk_push_str_fmt(output, "count: %u\n", node[1].expr_up_parent_mods.count);

			TekCompiler_debug_syntax_node_child(
				c, file, output, node, indent_level, "sub_expr", node[1].expr_up_parent_mods.sub_expr_rel_idx);
			break;

		case TekSynNodeKind_stmt_list_header:
		case TekSynNodeKind_expr_list_header:
			while (1) {
				TekCompiler_debug_syntax_node(c, file, output, &node[node->list_header.item_rel_idx], indent_level);
				if (node->list_header.next_rel_idx == 0) break;
				TekStk_push_str(output, "\n");
				node = &node[node->list_header.next_rel_idx];
				goto NEXT_NODE;
			}
			break;

		default:
			tek_abort("unhandled syntax node kind '%s'", TekSynNodeKind_strings[node[1].header.kind]);
	}
}

void TekCompiler_debug_syntax_tree(TekCompiler* c) {
	char token_name[128];

	TekStk(char) output = {0};

	TekFile* files = TekCompiler_files(c);
	uint32_t files_count = atomic_load(&c->files_count);
	for (uint32_t i = 0; i < files_count; i += 1) {
		TekFile* file = &files[i];
		TekSynNode* nodes = TekFile_syntax_tree_nodes(file);

		char* path = TekStrEntry_value(TekCompiler_strtab_get_entry(c, file->path_str_id));
		TekStk_push_str_fmt(&output, "########## FILE: %s ##########\n", path);

		TekCompiler_debug_syntax_node(c, file, &output, nodes, 0);
	}

	int res = tek_file_write(tek_debug_syntax_tree_path, output.TekStk_data, output.count);
	tek_assert(res == 0, "failed to write token file: %s", strerror(res));
	TekStk_deinit(&output);
}

TekCompilerError TekCompiler_compile_wait(TekCompiler* c) {
	TekMtx_lock(&c->wait_mtx);
	TekMtx_unlock(&c->wait_mtx);

	if (TekCompiler_has_errors(c)) {
		return TekCompilerError_compile_error;
	} else {
#if TEK_DEBUG_TOKENS
		TekCompiler_debug_tokens(c);
#endif

#if TEK_DEBUG_SYNTAX_TREE
		TekCompiler_debug_syntax_tree(c);
#endif
		return TekCompilerError_none;
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
	[TekErrorKind_gen_syn_mod_must_have_impl] = "a module must have an implementation, use the curly brace '{' after the mod keyword",
	[TekErrorKind_gen_syn_decl_mod_colon_must_follow_ident] = "a colon ':' must follow the declaration identifier",
	[TekErrorKind_gen_syn_decl_expected_keyword] = "expected a declaration item keyword like 'mod', 'struct', 'proc', 'enum', 'interf'",
	[TekErrorKind_gen_syn_entry_expected_to_end_with_a_new_line] = "entry must end with a new line so we can tell where it ends",
	[TekErrorKind_gen_syn_proc_expected_parentheses] = "parentheses '(' must follow the 'proc' keyword to define some parameters",
	[TekErrorKind_gen_syn_proc_expected_parentheses_to_follow_arrow] = "parentheses '(' must follow the '->' to define some return parameters",
	[TekErrorKind_gen_syn_proc_params_cannot_have_vararg_in_return_params] = "we cannot have a vararg in the return parameters",
	[TekErrorKind_gen_syn_proc_params_unexpected_delimiter] = "expected ',' to declare another parameter or a ')' to finish declaring parameters",
	[TekErrorKind_gen_syn_type_unexpected_token] = "expected a type",
	[TekErrorKind_gen_syn_type_bounded_int_expected_pipe_and_bit_count] = "expected a '|' followed by the number of bits to use for this bounded integer",
	[TekErrorKind_gen_syn_expr_call_expected_close_parentheses] = "expected a parentheses ')' to finish the call expression",
	[TekErrorKind_gen_syn_type_array_expected_close_bracket] = "expected a bracket ']' to finish the array type",
	[TekErrorKind_gen_syn_expr_index_expected_close_bracket] = "expected a bracket ']' to finish the index expression",
	[TekErrorKind_gen_syn_expr_expected_close_parentheses] = "expected a parentheses ')' to finish the expression",
	[TekErrorKind_gen_syn_expr_loop_expected_curly_brace] = "expected a curly brace '{' to follow the loop keyword",
	[TekErrorKind_gen_syn_expr_array_expected_close_bracket] = "expected a bracket ']' to finish the array expression",
	[TekErrorKind_gen_syn_expr_if_else_unexpected_token] = "expected an '{' to declare an else block or a 'if' to declare an else if",
	[TekErrorKind_gen_syn_expr_match_must_define_cases] = "a match must define its cases, use the curly brace '{' a create a list of cases",
	[TekErrorKind_gen_syn_expr_match_unexpected_token] = "expected 'case' for a condition, 'else' for the else block, '{' to define a block for a case or '}' to finish the match expression",
	[TekErrorKind_gen_syn_expr_for_expected_in_keyword] = "expected 'in' keyword to follow the variable declartion of the for loop expression",
	[TekErrorKind_gen_syn_stmt_only_allow_var_decl] = "inside statement block, we can only have 'var' declartions",
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
	uintptr_t* line_code_start_indices = TekFile_line_code_start_indices(file);
	uint32_t code_idx_prev_line_start = line_code_start_indices[line == 1 ? 0 : line - 2];
	uint32_t code_idx_line_start = line_code_start_indices[line - 1];
	uint32_t code_idx_next_line_start = line_code_start_indices[line >= file->lines_count ? file->lines_count - 1 : line];
	uint32_t code_idx_next_next_line_start = line_code_start_indices[line + 1 >= file->lines_count ? file->lines_count - 1 : line + 1];

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

void TekCompiler_error_string_virt_mem(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors, TekError* e) {
	tek_abort("unimplemented");
}

void TekCompiler_error_string_single(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors, TekError* e) {
	TekFile* file = TekCompiler_file_get(c, e->args[0].file_id);
	TekTokenLoc* loc = &TekFile_token_locs(file)[e->args[0].token_idx];

	TekCompiler_error_string_error_line(string_out, e->kind, use_ascii_colors);
	TekCompiler_error_string_code(c, string_out, file, loc->line, loc->column, loc->code_idx_start, loc->code_idx_end, use_ascii_colors);
}

void TekCompiler_error_string_double(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors, TekError* e) {
	TekCompiler_error_string_error_line(string_out, e->kind, use_ascii_colors);
	char** info_lines = TekErrorKind_double_info_lines[e->kind];

	TekFile* file = TekCompiler_file_get(c, e->args[0].file_id);
	TekTokenLoc* loc = &TekFile_token_locs(file)[e->args[0].token_idx];
	TekCompiler_error_string_info_line(string_out, info_lines[0], use_ascii_colors);
	TekCompiler_error_string_code(c, string_out, file, loc->line, loc->column, loc->code_idx_start, loc->code_idx_end, use_ascii_colors);

	file = TekCompiler_file_get(c, e->args[1].file_id);
	loc = &TekFile_token_locs(file)[e->args[1].token_idx];
	TekCompiler_error_string_info_line(string_out, info_lines[1], use_ascii_colors);
	TekCompiler_error_string_code(c, string_out, file, loc->line, loc->column, loc->code_idx_start, loc->code_idx_end, use_ascii_colors);
}

void TekCompiler_errors_string(TekCompiler* c, TekStk(char)* string_out, TekBool use_ascii_colors) {
	TekError* errors = TekCompiler_errors(c);
	uint32_t errors_count = atomic_load(&c->errors_count);
	for (uint32_t i = 0; i < errors_count; i += 1) {
		TekError* e = &errors[i];
		switch (e->kind) {
			case TekErrorKind_invalid_file_path:
				TekCompiler_error_string_file_path_errno(c, string_out, use_ascii_colors, e);
				break;
			case TekErrorKind_virt_mem:
			case TekErrorKind_lexer_file_read_failed:
				TekCompiler_error_string_virt_mem(c, string_out, use_ascii_colors, e);
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
			case TekErrorKind_gen_syn_mod_must_have_impl:
			case TekErrorKind_gen_syn_decl_mod_colon_must_follow_ident:
			case TekErrorKind_gen_syn_decl_expected_keyword:
			case TekErrorKind_gen_syn_entry_expected_to_end_with_a_new_line:
			case TekErrorKind_gen_syn_proc_expected_parentheses:
			case TekErrorKind_gen_syn_proc_expected_parentheses_to_follow_arrow:
			case TekErrorKind_gen_syn_proc_params_cannot_have_vararg_in_return_params:
			case TekErrorKind_gen_syn_proc_params_unexpected_delimiter:
			case TekErrorKind_gen_syn_type_unexpected_token:
			case TekErrorKind_gen_syn_type_bounded_int_expected_pipe_and_bit_count:
			case TekErrorKind_gen_syn_expr_call_expected_close_parentheses:
			case TekErrorKind_gen_syn_type_array_expected_close_bracket:
			case TekErrorKind_gen_syn_expr_index_expected_close_bracket:
			case TekErrorKind_gen_syn_expr_expected_close_parentheses:
			case TekErrorKind_gen_syn_expr_loop_expected_curly_brace:
			case TekErrorKind_gen_syn_expr_array_expected_close_bracket:
			case TekErrorKind_gen_syn_expr_if_else_unexpected_token:
			case TekErrorKind_gen_syn_expr_match_must_define_cases:
			case TekErrorKind_gen_syn_expr_match_unexpected_token:
			case TekErrorKind_gen_syn_expr_for_expected_in_keyword:
			case TekErrorKind_gen_syn_stmt_only_allow_var_decl:
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

TekError* TekCompiler_error_add(TekCompiler* c, TekErrorKind kind) {
	TekError* errors = TekCompiler_errors(c);
	uint32_t idx = atomic_fetch_add(&c->errors_count, 1);
	TekError* e = &errors[idx];
	e->kind = kind;
	return e;
}

void TekCompiler_signal_stop(TekCompiler* c) {
	// set this flag so all workers will stop working
	atomic_fetch_or(&c->flags, TekCompilerFlags_is_stopping);
}

TekBool TekCompiler_out_of_memory(TekCompiler* c) {
	return atomic_load(&c->flags) & TekCompilerFlags_out_of_memory;
}

TekBool TekCompiler_has_errors(TekCompiler* c) {
	if (atomic_load(&c->flags) & TekCompilerFlags_out_of_memory)
		return tek_true;

	return atomic_load(&c->errors_count) > 0;
}

