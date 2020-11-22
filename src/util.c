#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <sys/mman.h> // mmap etc
#endif


//===========================================================================================
//
//
// misc helpers
//
//
//===========================================================================================

noreturn void _tek_abort(const char* file, int line, const char* func, char* assert_test, char* message_fmt, ...) {
	if (assert_test) {
		fprintf(stderr, "assertion failed: %s\nmessage: ", assert_test);
	} else {
		fprintf(stderr, "abort reason: ");
	}

	va_list args;
	va_start(args, message_fmt);
	vfprintf(stderr, message_fmt, args);
	va_end(args);

	fprintf(stderr, "\nfile: %s:%d\n%s\n", file, line, func);
	abort();
}

uint32_t tek_utf8_codepoint_to_utf32(char* utf8_str, int32_t* utf32_out) {
	uint32_t bytes = 0;
	if (0xf0 == (0xf8 & utf8_str[0])) {
		// 4 byte utf8 codepoint
		*utf32_out = ((0x07 & utf8_str[0]) << 18) | ((0x3f & utf8_str[1]) << 12) |
		((0x3f & utf8_str[2]) << 6) | (0x3f & utf8_str[3]);
		bytes = 4;
	} else if (0xe0 == (0xf0 & utf8_str[0])) {
		// 3 byte utf8 codepoint
		*utf32_out =
		((0x0f & utf8_str[0]) << 12) | ((0x3f & utf8_str[1]) << 6) | (0x3f & utf8_str[2]);
		bytes = 3;
	} else if (0xc0 == (0xe0 & utf8_str[0])) {
		// 2 byte utf8 codepoint
		*utf32_out = ((0x1f & utf8_str[0]) << 6) | (0x3f & utf8_str[1]);
		bytes = 2;
	} else {
		// 1 byte utf8 codepoint otherwise
		*utf32_out = utf8_str[0];
		bytes = 1;
	}

	return bytes;
}

//===========================================================================================
//
//
// functions for numbers
//
//
//===========================================================================================

uint8_t tek_digit_char_to_int(uint8_t ch, uint8_t radix) {
    tek_assert(radix <= 36, "'radix' must be less than or equal to 36, as that is all the digits and alpha characters used up");
    uint8_t digit;
    if (radix <= 10) {
        if (ch < '0' || ch > '9') { return UINT8_MAX; }
        digit = ch - (uint8_t)'0';
    } else {
        switch (ch) {
            case '0'...'9': digit = ch - (uint8_t)'0'; break;
            case 'A'...'Z': digit = ch - (uint8_t)'A' + 10; break;
            case 'a'...'z': digit = ch - (uint8_t)'a' + 10; break;
            default: return UINT8_MAX;
        }
    }

    if (digit >= radix) { return UINT8_MAX; }
    return digit;
}

uint8_t tek_digit_to_char(uint8_t digit, uint8_t radix) {
    tek_assert(digit < radix, "'digit' is out of the bounds of the 'radix'");
    tek_assert(radix <= 36, "'radix' must be less than or equal to 36, as that is all the digits and alpha characters used up");

	if (digit < 10) {
		return '0' + digit;
	} else {
		digit -= 10;
		return 'A' + digit;
	}
}

TekBool tek_u128_checked_add(__uint128_t a, __uint128_t b, __uint128_t* res_out) {
	if (b > (((__uint128_t)-1) - a)) { return tek_false; }
	*res_out = a + b;
	return tek_true;
}

TekBool tek_u128_checked_mul(__uint128_t a, __uint128_t b, __uint128_t* res_out) {
	__uint128_t r = a * b;

	if (a != 0 && b != 0 && a != r / b)
		return tek_false;

	*res_out = r;
	return tek_true;
}

TekBool tek_s128_checked_add(__int128_t a, __int128_t b, __int128_t* res_out) {
	if (b < (((__uint128_t)-1) - a)) { return tek_false; }
	*res_out = a - b;
	return tek_true;
}

TekBool tek_s128_checked_mul(__int128_t a, __int128_t b, __int128_t* res_out) {
	__int128_t r = a * b;

	if (a != 0 && b != 0 && a != r / b)
		return tek_false;

	*res_out = r;
	return tek_true;
}

TekNumParseRes tek_u128_parse(char* str, uint32_t str_len, uint8_t radix, __uint128_t* value_out) {
    *value_out = 0;
    if (str_len == 0) { return TekNumParseRes_nan; }
    __uint128_t res = 0;
    uint32_t idx = 0;
    for (; idx < str_len; idx += 1) {
        uint8_t digit = tek_digit_char_to_int(str[idx], radix);
        if (digit == UINT8_MAX)
            break;

        // move the digits left
        if (!tek_u128_checked_mul(res, radix, &res)) { return TekNumParseRes_overflow; }

        // add the new digit
        if (!tek_u128_checked_add(res, digit, &res)) { return TekNumParseRes_overflow; }

    }
    *value_out = res;

    return idx;
}

TekNumParseRes tek_s128_parse(char* str, uint32_t str_len, uint8_t radix, __int128_t* value_out) {
    *value_out = 0;
    if (str_len == 0) { return TekNumParseRes_nan; }

    TekBool is_neg = str[0] == '-';
    if (is_neg && str_len == 1) { return TekNumParseRes_nan; }

    uint32_t idx = is_neg ? 1 : 0;
	__int128_t sign = is_neg ? -1.0 : 1.0;

    __int128_t res = 0;
    for (; idx < str_len; idx += 1) {
        uint8_t digit = tek_digit_char_to_int(str[idx], radix);
        if (digit == UINT8_MAX) {
            break;
        }

        /* move the digits left */
        if (!tek_s128_checked_mul(res, radix, &res)) { return TekNumParseRes_overflow; }

        /* add the new digit */
        if (!tek_s128_checked_add(res, digit, &res)) { return TekNumParseRes_overflow; }

    }
    *value_out = sign * res;

    return idx;
}

TekNumParseRes tek_f128_parse(char* str, uint32_t str_len, __float128* value_out) {
    *value_out = 0;
    if (str_len == 0) { return TekNumParseRes_nan; }

    TekBool is_neg = str[0] == '-';
    if (is_neg && str_len == 1) { return TekNumParseRes_nan; }
    uint32_t idx = is_neg ? 1 : 0;
	__float128 sign = is_neg ? -1.0 : 1.0;

    __float128 res = 0.0;
	/* parse the integer part */
    for (; idx < str_len; idx += 1) {
        uint8_t digit = tek_digit_char_to_int(str[idx], 10);
        if (digit == UINT8_MAX) {
            break;
        }

        /* move the digits left */
		res *= 10.0;

        /* add the new digit */
		res += digit;

    }

	/* parse the mantissa */
	if (str[idx] == '.') {
		__float128 pow = 10.0;
		for (; idx < str_len; idx += 1) {
			uint8_t digit = tek_digit_char_to_int(str[idx], 10);
			if (digit == UINT8_MAX) {
				break;
			}

			/* add the new digit */
			res += digit / pow;
			pow *= 10.0;

		}
	}

	__float128 exp_scale = 1.0;
	TekBool neg_exp = tek_false;
	// parse the exponent
	if (idx < str_len && ((str[idx] == 'e') || (str[idx] == 'E'))) {
		idx += 1;

		if (str[idx] == '-') {
			neg_exp = tek_true;
			idx += 1;
		} else if (str[idx] == '+') {
			idx += 1;
		}

		uint32_t exp = 0;
		for (; idx < str_len; idx += 1) {
			uint8_t digit = tek_digit_char_to_int(str[idx], 10.0);
			if (digit == UINT8_MAX) {
				break;
			}
			exp = exp * 10.0 + (uint32_t)digit;
		}
		if (exp > 308) exp = 308;

		while (exp >= 50) { exp_scale *= 1e50; exp -= 50; }
		while (exp >=  8) { exp_scale *= 1e8;  exp -=  8; }
		while (exp >   0) { exp_scale *= 10.0; exp -=  1; }
	}

    *value_out = sign * (neg_exp ? (res / exp_scale) : (res * exp_scale));

    return idx;
}

//===========================================================================================
//
//
// memory allocation
//
//
//===========================================================================================

void* tek_realloc(void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align) {
	uint32_t fail_num = 1;
	while (1) {
		void* new_ptr = tek_alctor.fn(tek_alctor.data, ptr, old_size, size, align);
		if (new_ptr) return new_ptr;

		// no out of memory handler so return NULL
		if (!tek_out_of_mem_handler.fn) return NULL;


		//
		// run the out of memory handler to give the application a chance
		// to free some memory or change the allocator.
		if (!tek_out_of_mem_handler.fn(tek_out_of_mem_handler.data, size, fail_num)) {
			break;
		}

		fail_num += 1;
	}

	tek_abort("failed to allocate %zu bytes\n", size);
}

void tek_dealloc(void* ptr, uintptr_t old_size, uintptr_t align) {
	tek_alctor.fn(tek_alctor.data, ptr, old_size, 0, align);
}

//===========================================================================================
//
//
// memory allocation - custom allocator interface
//
//
//===========================================================================================

#ifdef _WIN32

// fortunately, windows provides aligned memory allocation function
void* tek_system_alloc_fn(void* alloc_data, void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align) {
	if (!ptr && size == 0) {
		// reset not supported so do nothing
		return NULL;
	} else if (!ptr) {
		// allocate
		return _aligned_malloc(size, align);
	} else if (ptr && size > 0) {
		// reallocate
		return _aligned_realloc(ptr, size, align);
	} else {
		// deallocate
		return _aligned_free(ptr);
	}
}

#else // posix

//
// the C11 standard says malloc is guaranteed aligned to alignof(max_align_t).
// so allocations that have alignment less than or equal to this, can directly call malloc, realloc and free.
// luckly this is for most allocations.
//
// but there are alignments that are larger (for example is intel AVX256 primitives).
// these require calls aligned_alloc.
void* tek_system_alloc_fn(void* alloc_data, void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align) {
	if (!ptr && size == 0) {
		// reset not supported so do nothing
		return NULL;
	} else if (!ptr) {
		// allocate
		if (align <= alignof(max_align_t)) {
			return malloc(size);
		}

		// size must be multiple of align, so round up
		return aligned_alloc(align, (uintptr_t)tek_ptr_round_up_align((void*)size, align));
	} else if (ptr && size > 0) {
		// reallocate
		if (align <= alignof(max_align_t)) {
			return realloc(ptr, size);
		}

		//
		// there is no aligned realloction on any posix based systems :(
		void* new_ptr = aligned_alloc(align, (uintptr_t)tek_ptr_round_up_align((void*)size, align));
		// size must be multiple of align, so round up
		memcpy(new_ptr, ptr, tek_min(old_size, size));
		free(ptr);
		return new_ptr;
	} else {
		// deallocate
		free(ptr);
		return NULL;
	}
}

#endif

thread_local TekAlctor tek_alctor = TekAlctor_system;

//===========================================================================================
//
//
// memory allocation - out of memory handler (optional)
//
//
//===========================================================================================

thread_local TekOutOfMemHandler tek_out_of_mem_handler = TekOutOfMemHandler_null;

//===========================================================================================
//
//
// memory allocation - stack LIFO
//
//
//===========================================================================================

void _TekStk_init_with_cap(_TekStk* stk, uint32_t cap, uint32_t elmt_size, uint32_t elmt_align) {
	*stk = (_TekStk){0};
	_TekStk_resize_cap(stk, cap, elmt_size, elmt_align);
}

void _TekStk_deinit(_TekStk* stk, uint32_t elmt_size, uint32_t elmt_align) {
	tek_dealloc(stk->data, stk->cap * elmt_size, elmt_align);
	stk->data = NULL;
	stk->count = 0;
	stk->cap = 0;
}

void* _TekStk_get(_TekStk* stk, uint32_t idx, uint32_t elmt_size, const char* file, int line, const char* func) {
	tek_assert_loc(file, line, func, idx < stk->count, "idx '%u' is out of bounds for a stack of count '%u'", idx, stk->count);
	return tek_ptr_add(stk->data, idx * elmt_size);
}

void _TekStk_resize(_TekStk* stk, uint32_t new_count, TekBool zero, uint32_t elmt_size, uint32_t elmt_align) {
	if (stk->cap < new_count) {
		_TekStk_resize_cap(stk, tek_max(new_count, stk->cap * 2), elmt_size, elmt_align);
	}

	uintptr_t count = stk->count;
	if (zero && new_count > count) {
		memset(tek_ptr_add(stk->data, count * (uintptr_t)elmt_size), 0, ((uintptr_t)new_count - count) * (uintptr_t)elmt_size);
	}

	stk->count = new_count;
}

void _TekStk_resize_cap(_TekStk* stk, uint32_t new_cap, uint32_t elmt_size, uint32_t elmt_align) {
	new_cap = tek_max(tek_max(TekStk_min_cap, new_cap), stk->count);
	if (stk->cap == new_cap) return;

	uintptr_t size = (uintptr_t)stk->cap * (uintptr_t)elmt_size;
	uintptr_t new_size = (uintptr_t)new_cap * (uintptr_t)elmt_size;
	void* ptr = tek_realloc(stk->data, size, new_size, elmt_align);

	stk->data = ptr;
	stk->cap = new_cap;
}

void* _TekStk_insert_many(_TekStk* stk, uint32_t idx, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align) {
	tek_assert(idx <= stk->count, "insert idx '%u' must be less than or equal to count of '%u'", idx, stk->count);

    uint32_t requested_count = stk->count + elmts_count;
    if (stk->cap < requested_count) {
		_TekStk_resize_cap(stk, tek_max(requested_count, stk->cap * 2), elmt_size, elmt_align);
    }

    void* dst = tek_ptr_add(stk->data, idx * elmt_size);

	// shift the elements from idx to (idx + elmts_count), to the right
	// to make room for the elements
    memmove(tek_ptr_add(dst, elmts_count * elmt_size), dst, (uintptr_t)(stk->count - idx) * (uintptr_t)elmt_size);

	if (elmts) {
		// copy the elements to the stack
		memcpy(dst, elmts, (elmts_count * elmt_size));
	}

    stk->count += elmts_count;
	return dst;
}

void* _TekStk_push_many(_TekStk* stk, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align) {
	uint32_t idx = stk->count;
	uint32_t new_count = stk->count + elmts_count;
	if (new_count > stk->cap) {
		_TekStk_resize_cap(stk, tek_max(new_count, stk->cap * 2), elmt_size, elmt_align);
	}
	stk->count = new_count;
	void* dst = tek_ptr_add(stk->data, idx * elmt_size);
	if (elmts) {
		memcpy(dst, elmts, elmt_size * elmts_count);
	}
	return dst;
}

uint32_t _TekStk_pop_many(_TekStk* stk, void* elmts_out, uint32_t elmts_count, uint32_t elmt_size) {
	elmts_count = tek_min(elmts_count, stk->count);
	stk->count -= elmts_count;
	if (elmts_out) {
		memcpy(elmts_out, tek_ptr_add(stk->data, stk->count * elmt_size), elmts_count * elmt_size);
	}

	return elmts_count;
}

void _TekStk_swap_remove_range(_TekStk* stk, uint32_t start, uint32_t end, void* elmts_out, uint32_t elmt_size) {
	tek_assert(start <= stk->count, "start idx '%u' must be less than the count of '%u'", start, stk->count);
	tek_assert(end <= stk->count, "end idx '%u' must be less than or equal to count of '%u'", end, stk->count);

	uint32_t remove_count = end - start;
	void* dst = tek_ptr_add(stk->data, start * elmt_size);
	if (elmts_out) memcpy(elmts_out, dst, remove_count * elmt_size);

	uint32_t src_idx = stk->count;
	stk->count -= remove_count;
	if (remove_count > stk->count) remove_count = stk->count;
	src_idx -= remove_count;

	void* src = tek_ptr_add(stk->data, src_idx * elmt_size);
	memmove(dst, src, remove_count * elmt_size);
}

void _TekStk_shift_remove_range(_TekStk* stk, uint32_t start, uint32_t end, void* elmts_out, uint32_t elmt_size) {
	tek_assert(start <= stk->count, "start idx '%u' must be less than the count of '%u'", start, stk->count);
	tek_assert(end <= stk->count, "end idx '%u' must be less than or equal to count of '%u'", end, stk->count);

	uint32_t remove_count = end - start;
	void* dst = tek_ptr_add(stk->data, start * elmt_size);
	if (elmts_out) memcpy(elmts_out, dst, remove_count * elmt_size);

	if (end < stk->count) {
		void* src = tek_ptr_add(dst, remove_count * elmt_size);
		memmove(dst, src, (stk->count - (start + remove_count)) * elmt_size);
	}
	stk->count -= remove_count;
}

char* TekStk_push_str(TekStk(char)* stk, char* str) {
	uint32_t len = strlen(str);
	return TekStk_push_many(stk, str, len);
}

char* TekStk_push_str_fmtv(TekStk(char)* stk, char* fmt, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);

	// add 1 so we have enough room for the null terminator that vsnprintf always outputs
	// vsnprintf will return -1 on an encoding error.
	uint32_t count = vsnprintf(NULL, 0, fmt, args_copy) + 1;
	va_end(args_copy);
	if (count <= 1) return stk->TekStk_data + stk->count;

	uint32_t required_cap = stk->count + count;
	if (required_cap > stk->cap) {
		TekStk_resize_cap(stk, required_cap);
	}

	char* ptr = stk->TekStk_data + stk->count;
	count = vsnprintf(ptr, count, fmt, args);
	stk->count += count;
	return ptr;
}

char* TekStk_push_str_fmt(TekStk(char)* stk, char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char* ptr = TekStk_push_str_fmtv(stk, fmt, args);
	va_end(args);
	return ptr;
}

//===========================================================================================
//
//
// memory allocation - double ended queue (ring buffer) LIFO &| FIFO
//
//
//===========================================================================================

static inline uint32_t _TekDeque_wrapping_add(uint32_t cap, uint32_t idx, uint32_t value) {
    uint32_t res = idx + value;
    if (res >= cap) res = value - (cap - idx);
    return res;
}

static inline uint32_t _TekDeque_wrapping_sub(uint32_t cap, uint32_t idx, uint32_t value) {
    uint32_t res = idx - value;
    if (res > idx) res = cap - (value - idx);
    return res;
}

void _TekDeque_init_with_cap(_TekDeque* deque, uint32_t cap, uint32_t elmt_size, uint32_t elmt_align) {
	*deque = (_TekDeque){0};
	_TekDeque_resize_cap(deque, cap, elmt_size, elmt_align);
}

void _TekDeque_deinit(_TekDeque* deque, uint32_t elmt_size, uint32_t elmt_align) {
	tek_dealloc(deque->data, deque->_cap * elmt_size, elmt_align);

	deque->data = NULL;
	deque->_cap = 0;
	deque->_front_idx = 0;
	deque->_back_idx = 0;
}

void* _TekDeque_get(_TekDeque* deque, uint32_t idx, uint32_t elmt_size) {
	uint32_t count = TekDeque_count(deque);
	tek_assert(idx < count, "idx '%u' is out of bounds for a deque of count '%u'", idx, count);
    idx = _TekDeque_wrapping_add(deque->_cap, deque->_front_idx, idx);
    return tek_ptr_add(deque->data, idx * elmt_size);
}

void _TekDeque_resize_cap(_TekDeque* deque, uint32_t cap, uint32_t elmt_size, uint32_t elmt_align) {
	// add one because the back_idx needs to point to the next empty element slot.
	cap += 1;
	cap = tek_max(TekDeque_min_cap, cap);
	uint32_t count = TekDeque_count(deque);
	if (cap < count) cap = count;
	if (cap == deque->_cap) return;

	uintptr_t bc = (uintptr_t)deque->_cap * (uintptr_t)elmt_size;
	uintptr_t new_bc = (uintptr_t)cap * (uintptr_t)elmt_size;
	void* data = tek_realloc(deque->data, bc, new_bc, elmt_align);
	deque->data = data;

	uint32_t old_cap = deque->_cap;
	deque->_cap = cap;

	// move the front_idx and back_idx around to resolve the gaps that could have been created after resizing the buffer
	//
    // A - no gaps created so no need to change
    // --------
    //   F     B        F     B
    // [ V V V . ] -> [ V V V . . . . ]
    //
    // B - less elements before back_idx than elements from front_idx, so copy back_idx after the front_idx
    //       B F                  F         B
    // [ V V . V V V ] -> [ . . . V V V V V . . . . . ]
    //
    // C - more elements before back_idx than elements from front_idx, so copy front_idx to the end
    //       B F           B           F
    // [ V V . V] -> [ V V . . . . . . V ]
    //
    if (deque->_front_idx <= deque->_back_idx) { // A
    } else if (deque->_back_idx  < old_cap - deque->_front_idx) { // B
        memcpy(tek_ptr_add(data, old_cap * elmt_size), data, deque->_back_idx * elmt_size);
        deque->_back_idx += old_cap;
        tek_debug_assert(deque->_back_idx > deque->_front_idx, "back_idx must come after front_idx");
    } else { // C
        uint32_t new_front_idx = cap - (old_cap - deque->_front_idx);
        memcpy(tek_ptr_add(data, new_front_idx * elmt_size), tek_ptr_add(data, deque->_front_idx * elmt_size), (old_cap - deque->_front_idx) * elmt_size);
        deque->_front_idx = new_front_idx;
        tek_debug_assert(deque->_back_idx < deque->_front_idx, "front_idx must come after back_idx");
    }

	tek_debug_assert(deque->_back_idx < cap, "back_idx must remain in bounds");
	tek_debug_assert(deque->_front_idx < cap, "front_idx must remain in bounds");
}

void _TekDeque_push_front_many(_TekDeque* deque, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align) {
	uint32_t new_count = TekDeque_count(deque) + elmts_count;
	if (deque->_cap < new_count + 1) {
		_TekDeque_resize_cap(deque, tek_max(deque->_cap * 2, new_count), elmt_size, elmt_align);
	}

	if (elmts) {
		if (elmts_count > deque->_front_idx) {
			//
			// there is enough elements that pushing on the front
			// will cause the front_idx to loop around.
			// so copy in two parts
			// eg. pushing 3 values
			//     F B                    B
			// [ . V . . . . . ] -> [ V V . . . V V ]
			uint32_t rem_count = elmts_count - deque->_front_idx;
			// copy to the end of the buffer
			memcpy(tek_ptr_add(deque->data, (deque->_cap - rem_count) * elmt_size), elmts, rem_count * elmt_size);
			// copy to the beginning of the buffer
			memcpy(deque->data, tek_ptr_add(elmts, rem_count * elmt_size), deque->_front_idx * elmt_size);
		} else {
			//
			// coping the elements can be done in a single copy
			memcpy(tek_ptr_add(deque->data, (deque->_front_idx - elmts_count) * elmt_size), elmts, elmts_count * elmt_size);
		}
	}

	deque->_front_idx = _TekDeque_wrapping_sub(deque->_cap, deque->_front_idx, elmts_count);
}

void _TekDeque_push_back_many(_TekDeque* deque, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align) {
	uint32_t new_count = TekDeque_count(deque) + elmts_count;
	if (deque->_cap < new_count + 1) {
		_TekDeque_resize_cap(deque, tek_max(deque->_cap * 2, new_count), elmt_size, elmt_align);
	}

	if (elmts) {
		if (deque->_cap < deque->_back_idx + elmts_count) {
			//
			// there is enough elements that pushing on the back
			// will cause the back_idx to loop around.
			// so copy in two parts
			// eg. pushing 3 values
			//             F B            B     F
			// [ . . . . . V . ] -> [ V V . . . V V ]
			uint32_t rem_count = deque->_cap - deque->_back_idx;
			// copy to the end of the buffer
			memcpy(tek_ptr_add(deque->data, deque->_back_idx * elmt_size), elmts, rem_count * elmt_size);
			// copy to the beginning of the buffer
			memcpy(deque->data, tek_ptr_add(elmts, rem_count * elmt_size), ((elmts_count - rem_count) * elmt_size));
		} else {
			//
			// coping the elements can be done in a single copy
			memcpy(tek_ptr_add(deque->data, deque->_back_idx * elmt_size), elmts, elmts_count * elmt_size);
		}
	}

	deque->_back_idx = _TekDeque_wrapping_add(deque->_cap, deque->_back_idx, elmts_count);
}

uint32_t _TekDeque_pop_front_many(_TekDeque* deque, void* elmts_out, uint32_t elmts_count, uint32_t elmt_size) {
    if (deque->_front_idx == deque->_back_idx) return 0;
	elmts_count = tek_min(elmts_count, TekDeque_count(deque));

	if (elmts_out) {
		if (deque->_cap < deque->_front_idx + elmts_count) {
			//
			// there is enough elements that popping from the front
			// will cause the front_idx to loop around.
			// so copy in two parts
			// eg. popping 4 values
			//         B   F                    F B
			// [ V V V . . V V ] -> [ . . . . . V . ]
			uint32_t rem_count = deque->_cap - deque->_front_idx;
			// copy from the end of the buffer
			memcpy(elmts_out, tek_ptr_add(deque->data, deque->_front_idx * elmt_size), rem_count * elmt_size);
			// copy from the beginning of the buffer
			memcpy(tek_ptr_add(elmts_out, rem_count * elmt_size), deque->data, ((elmts_count - rem_count) * elmt_size));
		} else {
			//
			// coping the elements can be done in a single copy
			memcpy(elmts_out, tek_ptr_add(deque->data, deque->_front_idx * elmt_size), elmts_count * elmt_size);
		}
	}

	deque->_front_idx = _TekDeque_wrapping_add(deque->_cap, deque->_front_idx, elmts_count);
	return elmts_count;
}

uint32_t _TekDeque_pop_back_many(_TekDeque* deque, void* elmts_out, uint32_t elmts_count, uint32_t elmt_size) {
    if (deque->_front_idx == deque->_back_idx) return 0;
	elmts_count = tek_min(elmts_count, TekDeque_count(deque));

	if (elmts_out) {
		if (elmts_count > deque->_back_idx) {
			//
			// there is enough elements that popping from the back
			// will cause the back_idx to loop around.
			// so copy in two parts
			// eg. popping 3 values
			//     B     F              F B
			// [ V . . . V V V ] -> [ . V . . . . . ]
			uint32_t rem_count = elmts_count - deque->_back_idx;
			// copy from the end of the buffer
			memcpy(elmts_out, tek_ptr_add(deque->data, (deque->_cap - rem_count) * elmt_size), rem_count * elmt_size);
			// copy from the beginning of the buffer
			memcpy(tek_ptr_add(elmts_out, rem_count * elmt_size), deque->data, deque->_back_idx * elmt_size);
		} else {
			//
			// coping the elements can be done in a single copy
			memcpy(elmts_out, tek_ptr_add(deque->data, (deque->_back_idx - elmts_count) * elmt_size), elmts_count * elmt_size);
		}
	}

	deque->_back_idx = _TekDeque_wrapping_sub(deque->_cap, deque->_back_idx, elmts_count);
	return elmts_count;
}


//===========================================================================================
//
//
// memory allocation - element pool
//
//
//===========================================================================================

static inline void _TekPool_assert_idx(_TekPool* pool, uint32_t idx) {
	tek_debug_assert(idx < pool->cap, "idx is out of the memory boundary of the pool. idx is '%u' but cap is '%u'", idx, pool->cap);
}

static inline TekBool _TekPool_is_allocated(_TekPool* pool, uint32_t idx) {
	_TekPool_assert_idx(pool, idx);
	uint8_t bit = 1 << (idx % 8);
	return (((uint8_t*)pool->data)[idx / 8] & bit) == bit;
}

static inline void _TekPool_set_allocated(_TekPool* pool, uint32_t idx) {
	_TekPool_assert_idx(pool, idx);
	uint8_t bit = 1 << (idx % 8);
	((uint8_t*)pool->data)[idx / 8] |= bit;
}

static inline void _TekPool_set_free(_TekPool* pool, uint32_t idx) {
	_TekPool_assert_idx(pool, idx);
	((uint8_t*)pool->data)[idx / 8] &= ~(1 << (idx % 8));
}

void _TekPool_reset(_TekPool* pool, uintptr_t elmt_size) {
	//
	// set all the bits to 0 so all elements are marked as free
	uint8_t* is_alloced_bitset = pool->data;
	memset(is_alloced_bitset, 0, pool->elmts_start_byte_idx);

	//
	// now go through and set up the link list, so every element points to the next.
	void* elmts = tek_ptr_add(pool->data, pool->elmts_start_byte_idx);
	uintptr_t cap = pool->cap;
	for (uintptr_t i = 0; i < cap; i += 1) {
		// + 2 instead of 1 because we use id's here and not indexes.
		*(uintptr_t*)tek_ptr_add(elmts, i * elmt_size) = i + 2;
	}
	pool->count = 0;
	pool->free_list_head_id = 1;
}

void _TekPool_expand(_TekPool* pool, uint32_t new_cap, uintptr_t elmt_size, uintptr_t elmt_align) {
	tek_assert(new_cap >= pool->cap, "tek pool can only expand");

	//
	// expand the pool, this is fine since we store id's everywhere.
	//
	uint32_t bitset_size = (pool->cap / 8) + 1;
	uintptr_t cap_bytes = ((uintptr_t)pool->cap * elmt_size) + pool->elmts_start_byte_idx;

	uint32_t new_bitset_size = (new_cap / 8) + 1;
	uint32_t new_elmts_start_byte_idx = (uintptr_t)tek_ptr_round_up_align((void*)(uintptr_t)new_bitset_size, elmt_align);
	uintptr_t new_cap_bytes = ((uintptr_t)new_cap * elmt_size) +
		new_elmts_start_byte_idx;

	void* new_data = tek_realloc(pool->data, cap_bytes, new_cap_bytes, elmt_align);
	pool->data = new_data;

	void* elmts = tek_ptr_add(new_data, pool->elmts_start_byte_idx);
	void* new_elmts = tek_ptr_add(new_data, new_elmts_start_byte_idx);

	//
	// shift the elements to their new elmts_start_byte_idx.
	memmove(new_elmts, elmts, (uintptr_t)pool->cap * elmt_size);
	// zero the new bits of the is_allocated_bitset.
	memset(elmts, 0, new_elmts_start_byte_idx - pool->elmts_start_byte_idx);
	// then zero all the new elements.
	memset(tek_ptr_add(new_elmts, (uintptr_t)pool->cap * elmt_size), 0,
		((uintptr_t)(new_cap - pool->cap) * elmt_size));

	//
	// setup the free list, by visiting each element and store an
	// index to the next element.
	for (uint32_t i = pool->cap; i < new_cap; i += 1) {
		*(uint32_t*)tek_ptr_add(new_elmts, i * elmt_size) = i + 2;
	}

	pool->free_list_head_id = pool->cap + 1;
	pool->cap = new_cap;
	pool->elmts_start_byte_idx = new_elmts_start_byte_idx;
}

void _TekPool_reset_and_populate(_TekPool* pool, void* elmts, uint32_t count, uintptr_t elmt_size, uintptr_t elmt_align) {
	_TekPool_reset(pool, elmt_size);
	if (pool->cap < count) {
		_TekPool_expand(pool, tek_max(pool->cap ? pool->cap * 2 : 64, count), elmt_size, elmt_align);
	}

	//
	// set the elements to allocated.
	// the last byte is set manually as only some of the bits will be on.
	memset(pool->data, 0xff, count / 8);
	uint32_t remaining_count = count % 8;
	if (remaining_count) ((uint8_t*)pool->data)[count / 8] = (1 << remaining_count) - 1;

	//
	// copy the elements and set the values in the pool structure
	memcpy(tek_ptr_add(pool->data, pool->elmts_start_byte_idx), elmts, (uintptr_t)count * elmt_size);
	pool->count = count;
	pool->free_list_head_id = count + 1;
}

void _TekPool_init(_TekPool* pool, uint32_t cap, uintptr_t elmt_size, uintptr_t elmt_align) {
	uintptr_t bitset_size = (cap / 8) + 1;
	uintptr_t elmts_start_byte_idx = (uintptr_t)tek_ptr_round_up_align((void*)bitset_size, elmt_align);
	uintptr_t cap_bytes = ((uintptr_t)cap * elmt_size) + elmts_start_byte_idx;
	pool->data = tek_alloc(cap_bytes, elmt_align);
	memset(tek_ptr_add(pool->data, elmts_start_byte_idx), 0, (uintptr_t)cap * elmt_size);

	pool->elmts_start_byte_idx = elmts_start_byte_idx;
	pool->cap = cap;
	_TekPool_reset(pool, elmt_size);
	pool->free_list_head_id = 1;
}

void _TekPool_deinit(_TekPool* pool, uintptr_t elmt_size, uintptr_t elmt_align) {
	uintptr_t bitset_size = (pool->cap / 8) + 1;
	uintptr_t elmts_start_byte_idx = (uintptr_t)tek_ptr_round_up_align((void*)bitset_size, elmt_align);
	uintptr_t cap = ((uintptr_t)pool->cap * elmt_size) + elmts_start_byte_idx;
	tek_dealloc(pool->data, cap, elmt_align);
	*pool = (_TekPool){0};
}

void* _TekPool_alloc(_TekPool* pool, uint32_t* id_out, uintptr_t elmt_size, uintptr_t elmt_align) {
	if (pool->count == pool->cap) {
		_TekPool_expand(pool, pool->cap ? pool->cap * 2 : 64, elmt_size, elmt_align);
	}

	//
	// allocate an element and remove it from the free list
	uintptr_t alloced_id = pool->free_list_head_id;
	tek_debug_assert(!_TekPool_is_allocated(pool, alloced_id - 1), "allocated element is in the free list of the pool");
	_TekPool_set_allocated(pool, alloced_id - 1);
	uint32_t* alloced_elmt = (uint32_t*)tek_ptr_add(pool->data, (uintptr_t)pool->elmts_start_byte_idx + ((alloced_id - 1) * elmt_size));
	pool->free_list_head_id = *alloced_elmt;

	pool->count += 1;
	*id_out = alloced_id;
	return alloced_elmt;
}

void _TekPool_assert_id(_TekPool* pool, uint32_t elmt_id) {
	tek_debug_assert(elmt_id, "elmt_id is null, cannot deallocate a null element");
	tek_debug_assert(elmt_id <= pool->cap, "elmt_id is out of the memory boundary of the pool. idx is '%u' but cap is '%u'",
		elmt_id - 1, pool->cap);
	tek_debug_assert(_TekPool_is_allocated(pool, elmt_id - 1), "cannot get pointer to a element that is not allocated");
}

void _TekPool_dealloc(_TekPool* pool, uint32_t elmt_id, uintptr_t elmt_size, uintptr_t elmt_align) {
	_TekPool_assert_id(pool, elmt_id);

	uint32_t* elmt_next_free_id = &pool->free_list_head_id;
	void* elmts = tek_ptr_add(pool->data, pool->elmts_start_byte_idx);
	//
	// the free list is stored in low to high order, to try to keep allocations near eachother.
	// move up the free list until elmt_id is less than that node.
	while (1) {
		uint32_t nfi = *elmt_next_free_id;
		if (nfi > elmt_id || nfi == pool->cap) break;
		elmt_next_free_id = (uint32_t*)tek_ptr_add(elmts, ((uintptr_t)nfi - 1) * elmt_size);
	}

	_TekPool_set_free(pool, elmt_id - 1);
	// point to the next element in the list
	*(uint32_t*)tek_ptr_add(pool->data, (uintptr_t)pool->elmts_start_byte_idx + ((uintptr_t)(elmt_id - 1) * elmt_size)) = *elmt_next_free_id;
	// get the previous element to point to this newly deallocated block
	*elmt_next_free_id = elmt_id;
	pool->count -= 1;
}

void* _TekPool_id_to_ptr(_TekPool* pool, TekPoolId elmt_id, uintptr_t elmt_size) {
	_TekPool_assert_id(pool, elmt_id);
	return tek_ptr_add(pool->data, (uintptr_t)pool->elmts_start_byte_idx + ((uintptr_t)(elmt_id - 1) * elmt_size));
}

TekPoolId _TekPool_ptr_to_id(_TekPool* pool, void* ptr, uintptr_t elmt_size) {
	return (tek_ptr_diff(ptr, tek_ptr_add(pool->data, (uintptr_t)pool->elmts_start_byte_idx)) / elmt_size) + 1;
}

//===========================================================================================
//
//
// memory allocation - key value stack
//
//
//===========================================================================================

static inline uintptr_t _TekKVStk_size(uint32_t cap, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	void* size = NULL;
	size = tek_ptr_add(size, (uintptr_t)cap * (uintptr_t)key_size);
	size = tek_ptr_add(size, (uintptr_t)cap * (uintptr_t)value_size);
	size = tek_ptr_add(size, tek_max(key_align, value_align));
	return (uintptr_t)size;
}

static inline void* _TekKVStk_values(void* data, uint32_t cap, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	data = tek_ptr_add(data, (uintptr_t)cap * (uintptr_t)key_size);
	data = tek_ptr_round_up_align(data, value_align);
	return data;
}

void _TekKVStk_init(_TekKVStk* kv_stk, uint32_t cap, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	*kv_stk = (_TekKVStk){0};
	_TekKVStk_resize_cap(kv_stk, cap, key_size, key_align, value_size, value_align);
}

void _TekKVStk_deinit(_TekKVStk* kv_stk, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	tek_dealloc(kv_stk->data, _TekKVStk_size(kv_stk->cap, key_size, key_align, value_size, value_align), key_align);
	*kv_stk = (_TekKVStk){0};
}

void* _TekKVStk_get_key(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size) {
	tek_assert(idx < kv_stk->count, "idx '%u' is out of bounds for a stack of count '%u'", idx, kv_stk->count);
	return tek_ptr_add(kv_stk->data, (uintptr_t)idx * (uintptr_t)key_size);
}

void* _TekKVStk_get_value(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	tek_assert(idx < kv_stk->count, "idx '%u' is out of bounds for a stack of count '%u'", idx, kv_stk->count);
	void* values = _TekKVStk_values(kv_stk->data, kv_stk->cap, key_size, key_align, value_size, value_align);
	return tek_ptr_add(values, (uintptr_t)idx * (uintptr_t)value_size);
}

uint32_t _TekKVStk_find_key_32(_TekKVStk* kv_stk, uint32_t start_idx, uint32_t end_idx, uint32_t key, uint32_t key_size) {
	tek_debug_assert(key_size == sizeof(uint32_t), "key_size is expected to be %u but got %u", sizeof(uint32_t), key_size);
	uint32_t* keys = kv_stk->data;
	for (uint32_t i = start_idx; i < end_idx; i += 1) {
		if (keys[i] == key) return i + 1;
	}
	return 0;
}

uint32_t _TekKVStk_find_key_64(_TekKVStk* kv_stk, uint32_t start_idx, uint32_t end_idx, uint64_t key, uint32_t key_size) {
	tek_debug_assert(key_size == sizeof(uint64_t), "key_size is expected to be %u but got %u", sizeof(uint64_t), key_size);
	uint64_t* keys = kv_stk->data;
	for (uint32_t i = start_idx; i < end_idx; i += 1) {
		if (keys[i] == key) return i + 1;
	}
	return 0;
}

void _TekKVStk_resize_cap(_TekKVStk* kv_stk, uint32_t new_cap, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	new_cap = tek_max(tek_max(TekStk_min_cap, new_cap), kv_stk->count);
	if (kv_stk->cap == new_cap) return;

	uint32_t copy_count = tek_min(kv_stk->cap, new_cap);

	uintptr_t new_size = _TekKVStk_size(new_cap, key_size, key_align, value_size, value_align);

	//
	// allocate a new buffer
	void* new_data = tek_alloc(new_size, key_align);

	if (kv_stk->data) {
		// copy the key over to the new buffer
		tek_copy_bytes(new_data, kv_stk->data, (uintptr_t)copy_count * (uintptr_t)key_size);

		//
		// now copy the values over to thew new buffer
		void* values = _TekKVStk_values(kv_stk->data, kv_stk->cap, key_size, key_align, value_size, value_align);
		void* new_values = _TekKVStk_values(new_data, new_cap, key_size, key_align, value_size, value_align);
		tek_copy_bytes(new_values, values, (uintptr_t)copy_count * (uintptr_t)value_size);

		//
		// dealloc the old buffer
		uintptr_t size = _TekKVStk_size(kv_stk->cap, key_size, key_align, value_size, value_align);
		tek_dealloc(kv_stk->data, size, key_align);
	}

	kv_stk->data = new_data;
	kv_stk->cap = new_cap;
}

void _TekKVStk_push(_TekKVStk* kv_stk, void* key, void* value, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	uint32_t idx = kv_stk->count;
	uint32_t new_count = kv_stk->count + 1;
	//
	// ensure we have enough capacity, if not then double the capacity.
	if (kv_stk->cap < new_count) {
		_TekKVStk_resize_cap(kv_stk, kv_stk->cap * 2, key_size, key_align, value_size, value_align);
	}
	kv_stk->count = new_count;

	//
	// get the destination pointer for the key and value and them copy them
	void* key_dst = _TekKVStk_get_key(kv_stk, idx, key_size);
	void* value_dst = _TekKVStk_get_value(kv_stk, idx, key_size, key_align, value_size, value_align);
	tek_copy_bytes(key_dst, key, key_size);
	tek_copy_bytes(value_dst, value, value_size);
}

void _TekKVStk_insert(_TekKVStk* kv_stk, uint32_t idx, void* key, void* value, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	tek_assert(idx <= kv_stk->count, "insert idx '%u' must be less than or equal to count of '%u'", idx, kv_stk->count);

	uint32_t new_count = kv_stk->count + 1;
	//
	// ensure we have enough capacity, if not then double the capacity.
	if (kv_stk->cap < new_count) {
		_TekKVStk_resize_cap(kv_stk, kv_stk->cap * 2, key_size, key_align, value_size, value_align);
	}
	uint32_t old_count = kv_stk->count;
	kv_stk->count = new_count;

	//
	// get the destination pointer for the key and value
	void* key_dst = _TekKVStk_get_key(kv_stk, idx, key_size);
	void* value_dst = _TekKVStk_get_value(kv_stk, idx, key_size, key_align, value_size, value_align);

	//
	// shift the keys and values to right by 1 to make room the entry we are inserting
	tek_copy_bytes(tek_ptr_add(key_dst, key_size), key_dst, (uintptr_t)(old_count - idx) * (uintptr_t)key_size);
	tek_copy_bytes(tek_ptr_add(value_dst, value_size), value_dst, (uintptr_t)(old_count - idx) * (uintptr_t)value_size);

	tek_copy_bytes(key_dst, key, key_size);
	tek_copy_bytes(value_dst, value, value_size);
}

void _TekKVStk_remove_swap(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	tek_assert(idx < kv_stk->count, "remove idx '%u' must be less than to count of '%u'", idx, kv_stk->count);

	void* key_dst = _TekKVStk_get_key(kv_stk, idx, key_size);
	void* value_dst = _TekKVStk_get_value(kv_stk, idx, key_size, key_align, value_size, value_align);

	uint32_t end_idx = kv_stk->count - 1;
	void* key_src = _TekKVStk_get_key(kv_stk, end_idx, key_size);
	void* value_src = _TekKVStk_get_value(kv_stk, end_idx, key_size, key_align, value_size, value_align);

	//
	// copy the key and value from the end to where @param(idx) points
	tek_copy_bytes(key_dst, key_src, key_size);
	tek_copy_bytes(value_dst, value_src, value_size);

	kv_stk->count -= 1;
}

void _TekKVStk_remove_shift(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align) {
	tek_assert(idx < kv_stk->count, "remove idx '%u' must be less than to count of '%u'", idx, kv_stk->count);

	//
	// shift the elements that are to the right of where @param(idx) points, left by 1.
	// this will overwrite the key and value where @param(idx) points.
	if (idx + 1 < kv_stk->count) {
		void* key_dst = _TekKVStk_get_key(kv_stk, idx, key_size);
		void* value_dst = _TekKVStk_get_value(kv_stk, idx, key_size, key_align, value_size, value_align);

		tek_copy_bytes(key_dst, tek_ptr_add(key_dst, key_size), (uintptr_t)(kv_stk->count - idx) * (uintptr_t)key_size);
		tek_copy_bytes(value_dst, tek_ptr_add(value_dst, value_size), (uintptr_t)(kv_stk->count - idx) * (uintptr_t)value_size);
	}

	kv_stk->count -= 1;
}

//===========================================================================================
//
//
// memory allocation - hash table
//
//
//===========================================================================================

TekHash tek_hash_fnv(char* bytes, uint32_t byte_count, TekHash hash) {
	char* bytes_end = bytes + byte_count;
	while (bytes < bytes_end) {
		hash = hash ^ *bytes;
#if TEK_HASH_64
		hash = hash * 0x00000100000001B3;
#else
		hash = hash * 0x01000193;
#endif
		bytes += 1;
	}
	return hash;
}

TekStrId TekStrTab_get_or_insert(TekStrTab* strtab, char* str, uint32_t str_len) {
	TekHash hash = tek_hash_fnv(str, str_len, 0);
	uint32_t str_idx = 0;
	//
	// loop and maybe find a matching hash
	uint32_t count = strtab->count;
	while (1) {
		TekStrId str_id = TekKVStk_find_key_hash(strtab, str_idx, count, hash);
		if (str_id == 0) {
			//
			// no match found so create a new value and add it to the string table
			str_id = strtab->count + 1;
			char* value = tek_alloc_array(char, str_len + sizeof(uint32_t));
			*(uint32_t*)value = str_len;
			tek_copy_bytes(value + sizeof(uint32_t), str, str_len);
			TekKVStk_push(strtab, &hash, &value);

			return str_id;
		} else {
			//
			// found a matching hash, so get the string value and compare it
			// with what is in the string table. if they match then return the string index.
			TekStrEntry entry = TekStrTab_get_entry(strtab, str_id);

			uint32_t len = TekStrEntry_len(entry);
			char* v = TekStrEntry_value(entry);

			if (len == str_len && memcmp(v, str, str_len) == 0)
				return str_id;

			str_idx = str_id;
		}
	}
}

//===========================================================================================
//
//
// memory allocation - virtual memory
//
//
//===========================================================================================

#ifdef __linux__
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <asm-generic/mman.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#elif _WIN32
// TODO: i have read that the windows headers can really slow down compile times.
// since the win32 api is stable maybe we should forward declare the functions and constants manually ourselves
#include <windows.h>
#endif

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
static int _tek_virt_mem_prot_unix(TekVirtMemProtection prot) {
	switch (prot) {
		case TekVirtMemProtection_no_access: return 0;
		case TekVirtMemProtection_read: return PROT_READ;
		case TekVirtMemProtection_read_write: return PROT_READ | PROT_WRITE;
		case TekVirtMemProtection_exec_read: return PROT_EXEC | PROT_READ;
		case TekVirtMemProtection_exec_read_write: return PROT_READ | PROT_WRITE | PROT_EXEC;
	}
	return 0;
}
#elif _WIN32
static DWORD _tek_virt_mem_prot_windows(TekVirtMemProtection prot) {
	switch (prot) {
		case TekVirtMemProtection_no_access: return PAGE_NOACCESS;
		case TekVirtMemProtection_read: return PAGE_READONLY;
		case TekVirtMemProtection_read_write: return PAGE_READWRITE;
		case TekVirtMemProtection_exec_read: return PAGE_EXECUTE_READ;
		case TekVirtMemProtection_exec_read_write: return PAGE_EXECUTE_READWRITE;
	}
	return 0;
}
#else
#error "unimplemented virtual memory API for this platform"
#endif

TekVirtMemError tek_virt_mem_get_last_error() {
#ifdef __linux__
	return errno;
#elif _WIN32
    return GetLastError();
#endif
}

TekVirtMemErrorStrRes tek_virt_mem_get_error_string(TekVirtMemError error, char* buf_out, uint32_t buf_out_len) {
#if _WIN32
	DWORD res = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)buf_out, buf_out_len, NULL);
	if (res == 0) {
		tek_abort("TODO handle error code: %u", res);
	}
#elif _GNU_SOURCE
	// GNU version (screw these guys for changing the way this works)
	char* buf = strerror_r(error, buf_out, buf_out_len);
	if (strcmp(buf, "Unknown error") == 0) {
		return TekVirtMemErrorStrRes_invalid_error_arg;
	}

	// if its static string then copy it to the buffer
	if (buf != buf_out) {
		strncpy(buf_out, buf, buf_out_len);

		uint32_t len = strlen(buf);
		if (len < buf_out_len) {
			return TekVirtMemErrorStrRes_not_enough_space_in_buffer;
		}
	}
#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
	// use the XSI standard behavior.
	int res = strerror_r(error, buf_out, buf_out_len);
	if (res != 0) {
		if (errno == EINVAL) {
			return TekVirtMemErrorStrRes_invalid_error_arg;
		} else if (errno == ERANGE) {
			return TekVirtMemErrorStrRes_not_enough_space_in_buffer;
		}
		tek_abort("unexpected errno: %u", errno);
	}
#else
#error "unimplemented virtual memory error string"
#endif
	return TekVirtMemErrorStrRes_success;
}

uintptr_t tek_virt_mem_page_size() {
#ifdef __linux__
	return getpagesize();
#elif _WIN32
	SYSTEM_INFO si;
    GetSystemInfo(&si);
	// this actually returns the page granularity and not the page size.
	// since Virtual{Alloc, Protect, Free}.lpAddress must be aligned to the page granularity (region of pages)
	return si.dwAllocationGranularity;
#endif
}

void* tek_virt_mem_reserve(void* requested_addr, uintptr_t size, TekVirtMemProtection protection) {
	int prot = _tek_virt_mem_prot_unix(protection);
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))

	// MAP_ANON = means map physical memory and not a file. it also means the memory will be initialized to zero
	// MAP_PRIVATE = keep memory private so child process cannot access them
	void* addr = mmap(requested_addr, size, prot, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	return addr == MAP_FAILED ? NULL : addr;
#else
#error "TODO implement virtual memory for this platform"
#endif
}

TekBool tek_virt_mem_protection_set(void* addr, uintptr_t size, TekVirtMemProtection protection) {
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
	int prot = _tek_virt_mem_prot_unix(protection);
	return mprotect(addr, size, prot) == 0;
#else
#error "TODO implement virtual memory for this platform"
#endif
}

TekBool tek_virt_mem_decommit(void* addr, uintptr_t size) {
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
	return madvise(addr, size, MADV_DONTNEED) == 0;
#else
#error "TODO implement virtual memory for this platform"
#endif
}

TekBool tek_virt_mem_release(void* addr, uintptr_t size) {
#ifdef __linux__
	return munmap(addr, size) == 0;
#else
#error "TODO implement virtual memory for this platform"
#endif

	return tek_true;
}

void* tek_virt_mem_map_file(char* path, TekVirtMemProtection protection, uintptr_t* size_out, TekVirtMemFileHandle* file_handle_out) {
	if (protection == TekVirtMemProtection_no_access)
		tek_abort("cannot map a file with no access");

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
	int prot = _tek_virt_mem_prot_unix(protection);

	int fd_flags = 0;
	switch (prot) {
		case TekVirtMemProtection_read:
		case TekVirtMemProtection_exec_read:
			fd_flags = O_RDONLY;
			break;
		case TekVirtMemProtection_read_write:
		case TekVirtMemProtection_exec_read_write:
			fd_flags = O_RDWR;
			break;
	}

	int fd = open(path, fd_flags);
	if (fd == -1) return 0;

	// get the size of the file
	struct stat s = {0};
	if (fstat(fd, &s) != 0) return 0;
	uintptr_t size = s.st_size;

	void* addr = mmap(NULL, size, prot, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	*size_out = size;
	*file_handle_out = fd;
	return addr;
#else
#error "TODO implement virtual memory for this platform"
#endif
}

TekBool tek_virt_mem_map_file_close(TekVirtMemFileHandle file_handle) {
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
	return close(file_handle) == 0;
#else
#error "TODO implement virtual memory for this platform"
#endif
}

//===========================================================================================
//
//
// memory allocation - arena allocator
//
//
//===========================================================================================


TekVirtMemError TekLinearAlctor_init(TekLinearAlctor* alctor) {
	void* data = tek_virt_mem_reserve(NULL, TekLinearAlctor_cap, TekVirtMemProtection_read_write);
	if (data == NULL) return tek_virt_mem_get_last_error();
	alctor->data = data;
	return 0;
}

void TekLinearAlctor_deinit(TekLinearAlctor* alctor) {
	tek_virt_mem_release(alctor->data, TekLinearAlctor_cap);
	alctor->data = NULL;
}

void TekLinearAlctor_reset(TekLinearAlctor* alctor) {
	tek_virt_mem_decommit(alctor->data, TekLinearAlctor_cap);
}

void* TekLinearAlctor_alloc(TekLinearAlctor* alctor, uintptr_t size, uintptr_t align) {
	tek_abort("unimplemented");
	/*
	while (1) {
		// an allocation gets the pointer by adding the position to the start of the buffer.
		void* ptr = tek_ptr_add(md.arena, alctor->pos);
		// rounds up the pointer so it aligned as requested.
		ptr = tek_ptr_round_up_align(ptr, align);

		uintptr_t next_pos = tek_ptr_diff(ptr, md.arena) + size;
		// checks to see if it fits in the arena.
		if (next_pos <= md.size) {
			// and just increments the position for the next allocation.
			alctor->pos = next_pos;
			return ptr;
		} else {
			uintptr_t arena_size = TekLinearAlctor_min_arena_size;
			if (size > TekLinearAlctor_min_arena_size) {
				arena_size = size;
			}
			TekLinearAlctor_alloc_next_arena(alctor, arena_size);
		}
	}
	*/
}

void* TekLinearAlctor_TekAlctor_fn(void* alctor_data, void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align) {
	tek_abort("unimplemented");
	/*
	TekLinearAlctor* alctor = alctor_data;
	if (!ptr && size == 0) {
		TekLinearAlctor_reset(alctor);
	} else if (!ptr) {
		return TekLinearAlctor_alloc(alctor, size, align);
	} else if (ptr && size > 0) {
		void* new_ptr =  TekLinearAlctor_alloc(alctor, size, align);
		tek_copy_bytes(new_ptr, ptr, tek_min(old_size, size));
		return new_ptr;
	} else {
		// ignore deallocate
	}

	return NULL;
	*/
}

//===========================================================================================
//
//
// Threading
//
//
//===========================================================================================

static char* tek_mtx_already_locked_msg = "attempting to unlock a mutex that is already unlocked, did you unlock this earlier or maybe from another thread?";

void TekMtx_lock(TekMtx* mtx) {
    int pass;

    for (int i = 0; i < tek_thread_sync_primitive_spin_iterations; i += 1) {
        pass = tek_false;
        // if we manage to lock it here return early
        if (atomic_compare_exchange_weak(&mtx->_locked, &pass, tek_true)) {
            return;
        }

        tek_cpu_relax();
    }

    // wait for the value at the address of &mtx->_locked to be set to false and FUTEX_WAKE to be signaled
    while (1) {
        if (syscall(SYS_futex, &mtx->_locked, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, tek_true, NULL) == -1) {
            // if the mtx->_locked has been set to false since our last check, then we dont tek_abort
            if (errno != EAGAIN) {
                tek_abort("failed to wait for mutex: %s", strerror(errno));
            }
        }

        pass = tek_false;
        // attempt to lock it and return if success
        if (atomic_compare_exchange_weak(&mtx->_locked, &pass, tek_true)) {
            return;
        }
    }
}

void TekMtx_unlock(TekMtx* mtx) {
    int pass = tek_true;
    if (!atomic_compare_exchange_weak(&mtx->_locked, &pass, tek_false)) {
        tek_abort(tek_mtx_already_locked_msg);
    }

    // attempt to wake a single thread that is waiting for the unlock
    if (syscall(SYS_futex, &mtx->_locked, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL) == -1) {
        tek_abort("failed to send a wake signal to mutex: %s", strerror(errno));
    }
}

void TekSpinMtx_lock(TekSpinMtx* mtx) {
    TekBool pass = tek_false;
    while (!atomic_compare_exchange_weak(&mtx->_locked, &pass, tek_true)) {
        pass = tek_false;
        tek_cpu_relax();
    }
}

void TekSpinMtx_unlock(TekSpinMtx* mtx) {
    TekBool pass = tek_true;
    if (!atomic_compare_exchange_weak(&mtx->_locked, &pass, tek_false)) {
        tek_abort(tek_mtx_already_locked_msg);
    }
}

void TekSpinRWLock_lock_for_read(TekSpinRWLock* lock) {
    while (1) {
        if (atomic_load(&lock->_write_access_count) > 0) {
            tek_cpu_relax();
            continue;
        }

        atomic_fetch_add(&lock->_read_access_count, 1);

        // check to see if a write access has not been assigned since our previous check
        // if so then undo the add and restart the loop
        if (atomic_load(&lock->_write_access_count) > 0) {
            atomic_fetch_sub(&lock->_read_access_count, 1);
            continue;
        }

        return;
    }
}

void TekSpinRWLock_lock_for_write(TekSpinRWLock* lock) {
    uint32_t queue_pos = atomic_fetch_add(&lock->_write_access_count, 1);
    uint32_t last_write_count = queue_pos;
    while (1) {
        if (atomic_load(&lock->_read_access_count) != 0) {
            tek_cpu_relax();
            continue;
        }

        uint16_t write_count = atomic_load(&lock->_write_access_count);
		if (write_count == 1) {
			// single writer so it is this thread
			return;
		}

		if (write_count < last_write_count) {
			// unlocking will reduce the write count.
			// queue_pos 0 will mean you are first in the queue
			queue_pos -= 1;
			if (queue_pos == 0) {
				return;
			}
		}

        tek_cpu_relax();
    }
}

void TekSpinRWLock_unlock_for_read(TekSpinRWLock* lock) {
    atomic_fetch_sub(&lock->_read_access_count, 1);
}

void TekSpinRWLock_unlock_for_write(TekSpinRWLock* lock) {
    atomic_fetch_sub(&lock->_write_access_count, 1);
}

uint32_t tek_atomic_find_key_32(_Atomic uint32_t* keys, uint32_t key, uint32_t start_idx, uint32_t end_idx) {
	for (uint32_t i = start_idx; i < end_idx; i += 1) {
		if (atomic_load(&keys[i]) == key) return i + 1;
	}
	return 0;
}

uint32_t tek_atomic_find_key_64(_Atomic uint64_t* keys, uint64_t key, uint32_t start_idx, uint32_t end_idx) {
	for (uint32_t i = start_idx; i < end_idx; i += 1) {
		if (atomic_load(&keys[i]) == key) return i + 1;
	}
	return 0;
}

//===========================================================================================
//
//
// file system
//
//
//===========================================================================================

int tek_file_exist(char* file_path) {
	if (access(file_path, F_OK) != -1) return 0;
	return errno;
}

int tek_file_path_normalize_resolve(char* path, char* path_buf_out) {
	if (realpath(path, path_buf_out) != NULL) return 0;
	return errno;
}

int tek_file_read(char* path, TekStk(char)* bytes_out) {
    FILE* file = fopen(path, "r");
    if (file == NULL) { return errno; }

	// seek the cursor to the end
    if (fseek(file, 0, SEEK_END) == -1) { goto ERR; }

	// get the location of the position we have seeked the cursor to.
	// this will tell us the file size
    long int file_size = ftell(file);
    if (file_size == -1) { goto ERR; }

	// seek the cursor back to the beginning
    if (fseek(file, 0, SEEK_SET) == -1) { goto ERR; }

	//
	// ensure the buffer has enough capacity
    if (bytes_out->cap < file_size) {
        TekStk_resize_cap(bytes_out, file_size);
    }

	// read the file in
    size_t read_size = fread(bytes_out->TekStk_data, 1, file_size, file);
    if (read_size != file_size) { goto ERR; }

    if (fclose(file) != 0) { return errno; }

    bytes_out->count = read_size;
    return 0;

ERR: {}
	int err = errno;
    fclose(file);
    return err;
}

int tek_file_write(char* path, void* data, uintptr_t size) {
	FILE* file = fopen(path, "w");
    if (file == NULL) { return errno; }

	size_t bytes_written = fwrite(data, 1, size, file);
	if (bytes_written != size) goto ERR;

    if (fclose(file) != 0) { return errno; }

	return 0;

ERR: {}
	int err = errno;
    fclose(file);
    return err;
}

