#ifndef TEK_UTIL_H
#define TEK_UTIL_H

#include <threads.h>

//===========================================================================================
//
//
// configuration - compile time flags and constants
//
//
//===========================================================================================

#define TEK_HASH_64 0

//===========================================================================================
//
//
// misc helpers
//
//
//===========================================================================================

#ifndef noreturn
#define noreturn _Noreturn
#endif

#ifndef thread_local
#define thread_local _Thread_local
#endif

#ifndef alignof
#define alignof _Alignof
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif

typedef uint8_t TekBool;
#define tek_false 0
#define tek_true 1

#define tek_min(a, b) \
   ({ typeof(a) _a = (a); \
       typeof(b) _b = (b); \
     _a < _b ? _a : _b; })

#define tek_max(a, b) \
   ({ typeof(a) _a = (a); \
       typeof(b) _b = (b); \
     _a > _b ? _a : _b; })

#define tek_clamp(v, min, max) \
   ({ typeof(min) _min = (min); \
       typeof(max) _max = (max); \
       typeof(v) _v = (v); \
     (_v > _max) ? _max : (_v < _min) ? _min : v; })

#define tek_swap(dst, src) \
   ({ typeof(dst) _tmp = (dst); \
		(dst) = (src); \
		(_tmp); })

noreturn void _tek_abort(const char* file, int line, const char* func, char* assert_test, char* message_fmt, ...);
#define tek_abort(message_fmt, ...) \
	_tek_abort(__FILE__, __LINE__, __func__, NULL, message_fmt, ##__VA_ARGS__);

#define tek_assert(cond, message_fmt, ...) \
	if (!(cond)) _tek_abort(__FILE__, __LINE__, __func__, #cond, message_fmt, ##__VA_ARGS__);

#define tek_assert_loc(file, line, func, cond, message_fmt, ...) \
	if (!(cond)) _tek_abort(file, line, func, #cond, message_fmt, ##__VA_ARGS__);

#if TEK_DEBUG_ASSERTIONS
#define tek_debug_assert tek_assert
#else
#define tek_debug_assert(cond, message_fmt, ...) (void)(cond)
#endif

#ifndef __cplusplus
#define tek_type_equal(a, b) __builtin_types_compatible_p(a, b)
#else
#define typeof decltype
#define tek_type_equal(a, b) std::is_same<a, b>()
#endif

#define tek_assert_type(data_ptr, elmt_ptr) \
   static_assert(tek_type_equal(void*, typeof(elmt_ptr)) || tek_type_equal(typeof(*(data_ptr)), typeof(*(elmt_ptr))), "type mismatch, the element pointer type does not match the type of the data")


uint32_t tek_utf8_codepoint_to_utf32(char* utf8_str, int32_t* utf32_out);

#define tek_ensure(expr) if (!(expr)) return 0;
#define tek_likely(x) __builtin_expect((x),1)
#define tek_unlikely(x) __builtin_expect((x),0)

//===========================================================================================
//
//
// functions for numbers
//
//
//===========================================================================================

#define tek_is_power_of_two(v) ((v) != 0) && (((v) & ((v) - 1)) == 0)
TekBool tek_u128_checked_add(__uint128_t a, __uint128_t b, __uint128_t* res_out);
TekBool tek_u128_checked_mul(__uint128_t a, __uint128_t b, __uint128_t* res_out);
TekBool tek_s128_checked_add(__int128_t a, __int128_t b, __int128_t* res_out);
TekBool tek_s128_checked_mul(__int128_t a, __int128_t b, __int128_t* res_out);

typedef enum {
    TekNumParseRes_nan = -1,
    TekNumParseRes_overflow = -2,
} TekNumParseRes;
TekNumParseRes tek_u128_parse(char* str, uint32_t str_len, uint8_t radix, __uint128_t* value_out);
TekNumParseRes tek_s128_parse(char* str, uint32_t str_len, uint8_t radix, __int128_t* value_out);
TekNumParseRes tek_f128_parse(char* str, uint32_t str_len, __float128* value_out);

//===========================================================================================
//
//
// memory and pointers
//
//
//===========================================================================================

// for X86/64 and ARM. maybe be different for other architectures.
#define tek_cache_line_size 64

static inline void* tek_ptr_round_up_align(void* ptr, uintptr_t align) {
	tek_debug_assert(tek_is_power_of_two(align), "align must be a power of two but got: %zu", align);
	return (void*)(((uintptr_t)ptr + (align - 1)) & ~(align - 1));
}

static inline void* tek_ptr_round_down_align(void* ptr, uintptr_t align) {
	tek_debug_assert(tek_is_power_of_two(align), "align must be a power of two but got: %zu", align);
	return (void*)((uintptr_t)ptr & ~(align - 1));
}

#define tek_ptr_add(ptr, by) (void*)((uintptr_t)(ptr) + (uintptr_t)(by))
#define tek_ptr_sub(ptr, by) (void*)((uintptr_t)(ptr) - (uintptr_t)(by))
#define tek_ptr_diff(to, from) ((char*)(to) - (char*)(from))
#define tek_zero_elmt(ptr) memset(ptr, 0, sizeof(*(ptr)))
#define tek_zero_elmts(ptr, elmts_count) memset(ptr, 0, sizeof(*(ptr)) * (elmts_count))
#define tek_copy_bytes(dst, src, byte_count) memmove(dst, src, byte_count)
#define tek_copy_elmts(dst, src, elmts_count) memmove(dst, src, elmts_count * sizeof(*(dst)))

#define tek_rel_idx_u(T, bits, to, from) _tek_rel_idx(to, from, sizeof(T), 0, (1 << bits) - 1, __FILE__, __LINE__, __func__)
#define tek_rel_idx_s(T, bits, to, from) _tek_rel_idx(to, from, sizeof(T), -(1 << (bits - 1)), (1 << (bits - 1)) - 1, __FILE__, __LINE__, __func__)
#define tek_rel_idx_u8(T, to, from) _tek_rel_idx(to, from, sizeof(T), 0, UINT8_MAX, __FILE__, __LINE__, __func__)
#define tek_rel_idx_u16(T, to, from) _tek_rel_idx(to, from, sizeof(T), 0, UINT16_MAX, __FILE__, __LINE__, __func__)
#define tek_rel_idx_s8(T, to, from) _tek_rel_idx(to, from, sizeof(T), INT8_MIN, INT8_MAX, __FILE__, __LINE__, __func__)
#define tek_rel_idx_s16(T, to, from) _tek_rel_idx(to, from, sizeof(T), INT16_MIN, INT16_MAX, __FILE__, __LINE__, __func__)
static inline int64_t _tek_rel_idx(void* to, void* from, uint32_t elmt_size, int64_t min, int64_t max, const char* file, int line, const char* func) {
	int64_t offset = (to - from) / elmt_size;
	if (offset < min || offset > max) {
		_tek_abort(file, line, func, NULL, "cannot create a relative index of '%zd' to fit in the range of '%zd' - '%zd'", offset, min, max);
	}
	return offset;
}

//===========================================================================================
//
//
// memory allocation
//
//
//===========================================================================================

//
// these macros will de/allocate a memory from the thread's allocator.
// memory will be uninitialized unless the allocator you have zeros the memory for you.
//
// if you allocate some memory, and you want deallocate or expand the memory.
// it is up to you provide the correct 'old_size' that you allocated the memory with in the first place.
// unless you know that the allocator you are using does not care about that.
//
// all @param(align), must be a power of two and greater than 0.
//

// allocates memory that can hold a single element of type @param(T).
// @return: an appropriately aligned pointer to that allocated memory location.
#define tek_alloc_elmt(T) (T*)tek_alloc(sizeof(T), alignof(T))

// deallocates memory that can hold a single element of typeof(*@param(ptr)).
#define tek_dealloc_elmt(ptr) tek_dealloc(ptr, sizeof(*(ptr)), alignof(typeof(*(ptr))))

// allocates memory that can hold an array of @param(count) number of @param(T) elements.
// @return: an appropriately aligned pointer to that allocated memory location.
#define tek_alloc_array(T, count) (T*)tek_alloc((count) * sizeof(T), alignof(T))

// reallocates memory that can hold an array of @param(old_count) to @param(count) number of typeof(*@param(ptr)) elements.
// the memory of @param(ptr)[0] up to @param(ptr)[old_size] is preserved.
// @return a pointer to allocated memory with enough bytes to store a single element of type typeof(*@param(ptr)).
#define tek_realloc_array(ptr, old_count, count) \
	(typeof(ptr))tek_realloc(ptr, (old_count) * sizeof(*(ptr)), (count) * sizeof(*(ptr)), alignof(typeof(*(ptr))))

// deallocates memory that can hold an array of @param(old_count) number of typeof(*@param(ptr)) elements.
#define tek_dealloc_array(ptr, old_count) tek_dealloc(ptr, (old_count) * sizeof(*(ptr)), alignof(typeof(*(ptr))))

// allocates @param(size) bytes of memory that is aligned to @param(align)
#define tek_alloc(size, align) tek_realloc(NULL, 0, size, align)

// reallocates @param(old_size) to @param(size) bytes of memory that is aligned to @param(align)
// the memory of @param(ptr) up to @param(ptr) + @param(old_size') is preserved.
// if @param(ptr) == NULL, then this will call the alloc function for current allocator instead
extern void* tek_realloc(void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align);

// deallocates @param(old_size) bytes of memory that is aligned to @param(align'
extern void tek_dealloc(void* ptr, uintptr_t old_size, uintptr_t align);

//===========================================================================================
//
//
// memory allocation - custom allocator interface
//
//
//===========================================================================================

// an allocation function for allocating, reallocating and deallocating memory.
/*
	if (!ptr && size == 0) {
		// reset
	} else if (!ptr) {
		// allocate
	} else if (ptr && size > 0) {
		// reallocate
	} else {
		// deallocate
	}
*/
// returns NULL on allocation failure
typedef void* (*TekAllocFn)(void* alctor_data, void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align);

//
// TekAlctor is the custom allocator data structure.
// is used to point to the implementation of a custom allocator.
//
// EXAMPLE of a local alloctor:
//
// typedef struct {
//     void* data;
//     uint32_t pos;
//     uint32_t size;
// } LinearAlctor;
//
// void* LinearAlctor_alloc_fn(void* alctor_data, void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align)
//
// // we zero the buffer so this allocator will allocate zeroed memory.
// char buffer[1024] = {0};
// LinearAlctor linear_alctor = { .data = buffer, .pos = 0, .size = sizeof(buffer) };
// TekAlctor alctor = { .data = &linear_alctor, .fn = LinearAlctor_alloc_fn };
//
typedef struct {
	// the allocation function, see the notes above TekAllocFn
	TekAllocFn fn;

	// the data is an optional pointer used to point to an allocator's data structure.
	// it is passed into the TekAllocFn as the first argument.
	// this field can be NULL if you are implementing a global allocator.
	void* data;
} TekAlctor;

void* tek_system_alloc_fn(void* alloc_data, void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align);
#define TekAlctor_system (TekAlctor){ .fn = tek_system_alloc_fn, .data = NULL }

//
// this is the thread's allocator that is used by all tek allocation functions
// you can set this directly but its advised to swap the allocator for some allocations
// and then restore back to the original one.
//
// EXAMPLE:
//
// TekAlctor old_alctor = tek_swap(tek_alctor, new_alctor);
// int* int_ptr = tek_alloc_elmt(int);
// float* float_ptr = tek_alloc_elmt(float);
// tek_swap(tek_alctor, old_alctor);
//
extern thread_local TekAlctor tek_alctor;

//===========================================================================================
//
//
// memory allocation - out of memory handler (optional)
//
//
//===========================================================================================

//
// is the out of memory handler has been set by the application.
// it is called when the tek_{alloc, realloc, alloc_elmt} fails.
// this is to give the application a chance to free some memory or change the allocator.
//
// @param data: is the current handler's TekOutOfMemHandler.data field.
// @param size: the number of bytes that was requested.
// @param fail_num: the number of times allocation has failed. this number starts on 1
//
// @return
//     tek_true: for when you have resolved the problem, the allocation will try again.
//                if you have not resolved the problem, the out of memory handler will be called again
//                with an incremented fail_num.
//     tek_false: on failure, will print an error to stderr and then abort.
typedef TekBool (*TekOutOfMemHandlerFn)(void* data, uintptr_t size, uint32_t fail_num);

//
// for implementation a out of memory handler, see the documentation for TekOutOfMemHandlerFn.
// applications should idealy be the only thing providing out of memory handlers.
//
// the default value of the out of memory handler is TekOutOfMemHandler_null.
// this will make the tek allocations functions return NULL on failure.
// providing an out of memory handler will ensure that the allocations function never return NULL.
//
// you can set this directly but its advised to swap the handler for some allocations
// and then restore back to the original one.
//
// EXAMPLE:
//
// TekOutOfMemHandler old_out_of_mem_handler = tek_swap(tek_out_of_mem_handler, new_out_of_mem_handler);
// int* int_ptr = tek_alloc_elmt(int);
// float* float_ptr = tek_alloc_elmt(float);
// tek_swap(tek_out_of_mem_handler, old_out_of_mem_handler);
//
typedef struct {
	TekOutOfMemHandlerFn fn;
	void* data;
} TekOutOfMemHandler;
extern thread_local TekOutOfMemHandler tek_out_of_mem_handler;
#define TekOutOfMemHandler_null (TekOutOfMemHandler){0};


//===========================================================================================
//
//
// memory allocation - stack LIFO
//
//
//===========================================================================================
//
// linear stack of elements.
// LIFO is the optimal usage, so push elements and pop them from the end.
// can be used as a growable array.
// elements are allocated using the Dynamic Allocation API that is defined lower down in this file.
//
// can store up to UINT32_MAX of elements (so UINT32_MAX * sizeof(T) bytes).
// this is so the structure is the size of 2 pointers instead of 3 on 64 bit machines.
// this helps keep data in cache that sits along side the stack structure.
// it is unlikely you will need more elements, but if you do.
// you can always copy and paste the code a run a custom solution.
//
// the default value of this structure is zeroed memory.
//
// TekStk API example usage:
//
// TekStk(int) stack_of_ints;
// TekStk_init_with_cap(int, &stack_of_ints, 64);
//
// int value = 22;
// TekStk_push(&stack_of_ints, &value);
//
// TekStk_pop(&stack_of_ints, &value);
//

#define TekStk(T) TekStk_##T
typedef struct {
	void* data;
	uint32_t count;
	uint32_t cap;
} _TekStk;

//
// you need to make sure that you use this macro in global scope
// to define a structure for the type you want to use.
//
// we have a funky name for the data field,
// so an error will get thrown if you pass the
// wrong structure into one of the macros below.
#define typedef_TekStk(T) \
typedef struct { \
	T* TekStk_data; \
	uint32_t count; \
	uint32_t cap; \
} TekStk_##T;

typedef_TekStk(char);
typedef_TekStk(short);
typedef_TekStk(int);
typedef_TekStk(long);
typedef_TekStk(float);
typedef_TekStk(double);
typedef_TekStk(size_t);
typedef_TekStk(ptrdiff_t);
typedef_TekStk(uint8_t);
typedef_TekStk(uint16_t);
typedef_TekStk(uint32_t);
typedef_TekStk(uint64_t);
typedef_TekStk(int8_t);
typedef_TekStk(int16_t);
typedef_TekStk(int32_t);
typedef_TekStk(int64_t);

#define TekStk_min_cap 4

// initializes an empty stack with a preallocated capacity of @param(cap) elements
#define TekStk_init_with_cap(stk, cap) _TekStk_init_with_cap((_TekStk*)stk, cap, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))
extern void _TekStk_init_with_cap(_TekStk* stk, uint32_t cap, uint32_t elmt_size, uint32_t elmt_align);

// deallocates the memory and sets the stack to being empty.
#define TekStk_deinit(stk) _TekStk_deinit((_TekStk*)stk, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))
extern void _TekStk_deinit(_TekStk* stk, uint32_t elmt_size, uint32_t elmt_align);

// @param idx: the index of the element you wish to get.
//             this function will abort if @param(idx) is out of bounds
// @return: a pointer to the element at @param(idx).
#define TekStk_get(stk, idx) ((typeof((stk)->TekStk_data))_TekStk_get((_TekStk*)stk, idx, sizeof(*(stk)->TekStk_data), __FILE__, __LINE__, __func__))
void* _TekStk_get(_TekStk* stk, uint32_t idx, uint32_t elmt_size, const char* file, int line, const char* func);

// @param idx: the index of the element from the back of the stack you wish to get.
//             this function will abort if @param(idx) is out of bounds
// @return: a pointer to the element at @param(idx) starting from the back of the stack.
#define TekStk_get_back(stk, idx) ((typeof((stk)->TekStk_data))_TekStk_get((_TekStk*)stk, (stk)->count - 1 - idx, sizeof(*(stk)->TekStk_data), __FILE__, __LINE__, __func__))

// this function will abort if the stack is empty
// @return: a pointer to the first element
#define TekStk_get_first(stk) ((typeof((stk)->TekStk_data))_TekStk_get((_TekStk*)stk, 0, sizeof(*(stk)->TekStk_data), __FILE__, __LINE__, __func__))

// this function will abort if the stack is empty
// @return: a pointer to the last element
#define TekStk_get_last(stk) ((typeof((stk)->TekStk_data))_TekStk_get((_TekStk*)stk, (stk)->count - 1, sizeof(*(stk)->TekStk_data), __FILE__, __LINE__, __func__))

#define TekStk_clear(stk) (stk)->count = 0;

// resizes the stack to have new_count number of elements.
// if new_count is greater than stk->cap, then this function will internally call TekStk_resize_cap to make more room.
// if new_count is greater than stk->count, new elements will be uninitialized,
// unless you want the to be zeroed by passing in zero == tek_true.
#define TekStk_resize(stk, new_count, zero) _TekStk_resize((_TekStk*)stk, new_count, zero, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))
extern void _TekStk_resize(_TekStk* stk, uint32_t new_count, TekBool zero, uint32_t elmt_size, uint32_t elmt_align);

// reallocates the capacity of the data to have enough room for @param(new_cap) number of elements.
// if @param(new_cap) is less than stk->count then new_cap is set to stk->count.
#define TekStk_resize_cap(stk, new_cap) _TekStk_resize_cap((_TekStk*)stk, new_cap, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))
extern void _TekStk_resize_cap(_TekStk* stk, uint32_t new_cap, uint32_t elmt_size, uint32_t elmt_align);

// insert element/s at the index position in the stack.
// the elements from the index position in the stack, will be shifted to the right.
// to make room for the inserted element/s.
// elmt/s can be NULL and a pointer to uninitialized memory is returned
// returns a pointer to the index position.
#define TekStk_insert(stk, idx, elmt) ({ tek_assert_type((stk)->TekStk_data, elmt); ((typeof((stk)->TekStk_data))_TekStk_insert_many((_TekStk*)stk, idx, elmt, 1, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))); })
#define _TekStk_insert(stk, idx, elmt, elmt_size, elmt_align) _TekStk_insert_many((_TekStk*)stk, idx, elmt, 1, elmt_size, elmt_align)
#define TekStk_insert_many(stk, idx, elmts, elmts_count) ({ tek_assert_type((stk)->TekStk_data, elmts); ((typeof((stk)->TekStk_data))_TekStk_insert_many((_TekStk*)stk, idx, elmts, elmts_count, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))); })
extern void* _TekStk_insert_many(_TekStk* stk, uint32_t idx, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align);

// pushes element/s onto the end of the stack
// elmt/s can be NULL and a pointer to uninitialized memory is returned
// returns a pointer to the first element that was added to the stack.
#define TekStk_push(stk, elmt) ({ tek_assert_type((stk)->TekStk_data, elmt); ((typeof((stk)->TekStk_data))_TekStk_push_many((_TekStk*)stk, elmt, 1, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))); })
#define _TekStk_push(stk, elmt, elmt_size, elmt_align) _TekStk_push_many((_TekStk*)stk, elmt, 1, elmt_size, elmt_align)
#define TekStk_push_many(stk, elmts, elmts_count) ({ tek_assert_type((stk)->TekStk_data, elmts); ((typeof((stk)->TekStk_data))_TekStk_push_many((_TekStk*)stk, elmts, elmts_count, sizeof(*(stk)->TekStk_data), alignof(typeof(*(stk)->TekStk_data)))); })
extern void* _TekStk_push_many(_TekStk* stk, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align);

// removes element/s from the end of the stack
// returns the number of elements popped from the stack.
// elements will be copied to elmts_out unless it is NULL.
#define TekStk_pop(stk, elmt_out) ({ tek_assert_type((stk)->TekStk_data, elmt_out); _TekStk_pop_many((_TekStk*)stk, elmt_out, 1, sizeof(*(stk)->TekStk_data)); })
#define _TekStk_pop(stk, elmt_out, elmt_size) _TekStk_pop_many((_TekStk*)stk, elmt_out, 1, elmt_size)
#define TekStk_pop_many(stk, elmts_out, elmts_count) ({ tek_assert_type((stk)->TekStk_data, elmts_out); _TekStk_pop_many((_TekStk*)stk, elmts_out, elmts_count, sizeof(*(stk)->TekStk_data)); })
extern uint32_t _TekStk_pop_many(_TekStk* stk, void* elmts_out, uint32_t elmts_count, uint32_t elmt_size);

// removes element/s from the start_idx up to end_idx (exclusively).
// elements at the end of the stack are moved to replace the removed elements.
// the original elements will be copied to elmts_out unless it is NULL.
#define TekStk_swap_remove(stk, idx, elmt_out) ({ tek_assert_type((stk)->TekStk_data, elmt_out); _TekStk_swap_remove_range((_TekStk*)stk, idx, (idx) + 1, elmt_out, sizeof(*(stk)->TekStk_data)); })
#define _TekStk_swap_remove(stk, idx, elmt_out, elmt_size) _TekStk_swap_remove_range((_TekStk*)stk, idx, (idx) + 1, elmt_out, elmt_size)
#define TekStk_swap_remove_range(stk, start_idx, end_idx, elmts_out) ({ tek_assert_type((stk)->TekStk_data, elmts_out); _TekStk_swap_remove_range((_TekStk*)stk, start_idx, end_idx, elmts_out, sizeof(*(stk)->TekStk_data)); })
extern void _TekStk_swap_remove_range(_TekStk* stk, uint32_t start_idx, uint32_t end_idx, void* elmts_out, uint32_t elmt_size);

// removes element/s from the start_idx up to end_idx (exclusively).
// elements that come after are shifted to the left to replace the removed elements.
// the original elements will be copied to elmts_out unless it is NULL.
#define TekStk_shift_remove(stk, idx, elmt_out) ({ tek_assert_type((stk)->TekStk_data, elmt_out); _TekStk_shift_remove_range((_TekStk*)stk, idx, (idx) + 1, elmt_out, sizeof(*(stk)->TekStk_data)); })
#define _TekStk_shift_remove(stk, idx, elmt_out, elmt_size) _TekStk_shift_remove_range((_TekStk*)stk, idx, (idx) + 1, elmt_out, elmt_size)
#define TekStk_shift_remove_range(stk, start_idx, end_idx, elmts_out) ({ tek_assert_type((stk)->TekStk_data, elmts_out); _TekStk_shift_remove_range((_TekStk*)stk, start_idx, end_idx, elmts_out, sizeof(*(stk)->TekStk_data)); })
extern void _TekStk_shift_remove_range(_TekStk* stk, uint32_t start_idx, uint32_t end_idx, void* elmts_out, uint32_t elmt_size);

// pushes the string on to the end of a byte stack.
// returns a pointer to the start where the string is placed in the stack
extern char* TekStk_push_str(TekStk(char)* stk, char* str);

// pushes the formatted string on to the end of a byte stack.
// returns a pointer to the start where the string is placed in the stack
extern char* TekStk_push_str_fmtv(TekStk(char)* stk, char* fmt, va_list args);
extern char* TekStk_push_str_fmt(TekStk(char)* stk, char* fmt, ...);


//===========================================================================================
//
//
// memory allocation - double ended queue (ring buffer) LIFO &| FIFO
//
//
//===========================================================================================
//
// designed to be used for FIFO or LIFO operations.
// elements are allocated using the Dynamic Allocation API that is defined lower down in this file.
//
// can store up to UINT32_MAX of elements (so UINT32_MAX * sizeof(T) bytes).
// this is so the structure is the size of 3 pointers instead of 4 on 64 bit machines.
// this helps keep data in cache that sits along side the deque structure.
// it is unlikely you will need more elements, but if you do.
// you can always copy and paste the code a run a custom solution.
//
// - empty when '_front_idx' == '_back_idx'
// - '_cap' is the number of allocated elements, but the deque can only hold '_cap' - 1.
// 		thi is because the _back_idx needs to point to the next empty element slot.
// - '_front_idx' will point to the item at the front of the queue
// - '_back_idx' will point to the item after the item at the back of the queue
//
// TekDeque API example usage:
//
// TekDeque(int) queue_of_ints;
// TekDeque_init_with_cap(&queue_of_ints, 64);
//
// int value = 22;
// TekDeque_push_back(&queue_of_ints, &value);
//
// TekDeque_pop_front(&queue_of_ints, &value);
#define TekDeque(T) TekDeque_##T
typedef struct {
	void* data;
	uint32_t _cap;
	uint32_t _front_idx;
	uint32_t _back_idx;
} _TekDeque;

//
// you need to make sure that you use this macro in global scope
// to define a structure for the type you want to use.
//
// we have a funky name for the data field,
// so an error will get thrown if you pass the
// wrong structure into one of the macros below.
#define typedef_TekDeque(T) \
typedef struct { \
	T* _TekDeque_data; \
	uint32_t _cap; \
	uint32_t _front_idx; \
	uint32_t _back_idx; \
} TekDeque_##T;

typedef_TekDeque(char);
typedef_TekDeque(short);
typedef_TekDeque(int);
typedef_TekDeque(long);
typedef_TekDeque(float);
typedef_TekDeque(double);
typedef_TekDeque(size_t);
typedef_TekDeque(ptrdiff_t);
typedef_TekDeque(uint8_t);
typedef_TekDeque(uint16_t);
typedef_TekDeque(uint32_t);
typedef_TekDeque(uint64_t);
typedef_TekDeque(int8_t);
typedef_TekDeque(int16_t);
typedef_TekDeque(int32_t);
typedef_TekDeque(int64_t);

#define TekDeque_min_cap 4

// initializes an empty deque with a preallocated capacity of 'cap' elements
#define TekDeque_init_with_cap(deque, cap) _TekDeque_init_with_cap((_TekDeque*)deque, cap, sizeof(*(deque)->_TekDeque_data), alignof(typeof(*(deque)->_TekDeque_data)))
extern void _TekDeque_init_with_cap(_TekDeque* deque, uint32_t cap, uint32_t elmt_size, uint32_t elmt_align);

// deallocates the memory and sets the deque to being empty.
#define TekDeque_deinit(deque) _TekDeque_deinit((_TekDeque*)deque, sizeof(*(deque)->_TekDeque_data), alignof(typeof(*(deque)->_TekDeque_data)))
extern void _TekDeque_deinit(_TekDeque* deque, uint32_t elmt_size, uint32_t elmt_align);

#define TekDeque_is_empty(deque) ((deque)->_back_idx == (deque)->_front_idx)

// returns the number of elements
#define TekDeque_count(deque) ((deque)->_back_idx >= (deque)->_front_idx ? (deque)->_back_idx - (deque)->_front_idx : (deque)->_back_idx + ((deque)->_cap - (deque)->_front_idx))

// returns the number of elements that can be stored in the queue before a reallocation is required.
#define TekDeque_cap(deque) ((deque)->_cap == 0 ? 0 : (deque)->_cap - 1)

// removes all the elements from the deque
#define TekDeque_clear(deque) (deque)->_front_idx = (deque)->_back_idx

// returns a pointer to the element at idx.
// this function will abort if idx is out of bounds
#define TekDeque_get(deque, idx) ((typeof((deque)->_TekDeque_data))_TekDeque_get((_TekDeque*)deque, idx, sizeof(*(deque)->_TekDeque_data)))
extern void* _TekDeque_get(_TekDeque* deque, uint32_t idx, uint32_t elmt_size);

// returns a pointer to the first element
// this function will abort if the deque is empty
#define TekDeque_get_first(deque) ((typeof((deque)->_TekDeque_data))_TekDeque_get((_TekDeque*)deque, 0, sizeof(*(deque)->_TekDeque_data)))
//
// returns a pointer to the last element
// this function will abort if the deque is empty
#define TekDeque_get_last(deque) ((typeof((deque)->_TekDeque_data))_TekDeque_get((_TekDeque*)deque, TekDeque_count(deque) - 1, sizeof(*(deque)->_TekDeque_data)))

// reallocates the capacity of the data to have enough room for new_cap number of elements.
// if new_cap is less than (deque)->count then new_cap is set to (deque)->count.
#define TekDeque_resize_cap(deque, cap) _TekDeque_resize_cap((_TekDeque*)deque, cap, sizeof(*(deque)->_TekDeque_data), alignof(typeof(*(deque)->_TekDeque_data)))
extern void _TekDeque_resize_cap(_TekDeque* deque, uint32_t cap, uint32_t elmt_size, uint32_t elmt_align);

// pushes element/s at the front of the deque
// elmt/s can be NULL and the new elements in the deque will be uninitialized.
#define TekDeque_push_front(deque, elmt) ({ tek_assert_type((deque)->_TekDeque_data, elmt); _TekDeque_push_front_many((_TekDeque*)deque, elmt, 1, sizeof(*(deque)->_TekDeque_data), alignof(typeof(*(deque)->_TekDeque_data))); })
#define _TekDeque_push_front(deque, elmt, elmt_size, elmt_align) _TekDeque_push_front_many((_TekDeque*)deque, elmt, 1, elmt_size, elmt_align)
#define TekDeque_push_front_many(deque, elmts, elmts_count) ({ tek_assert_type((deque)->_TekDeque_data, elmts); _TekDeque_push_front_many((_TekDeque*)deque, elmts, elmts_count, sizeof(*(deque)->_TekDeque_data), alignof(typeof(*(deque)->_TekDeque_data))); })
extern void _TekDeque_push_front_many(_TekDeque* deque, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align);

// pushes element/s at the back of the deque
// elmt/s can be NULL and the new elements in the deque will be uninitialized.
#define TekDeque_push_back(deque, elmt) ({ tek_assert_type((deque)->_TekDeque_data, elmt); _TekDeque_push_back_many((_TekDeque*)deque, elmt, 1, sizeof(*(deque)->_TekDeque_data), alignof(typeof(*(deque)->_TekDeque_data))); })
#define _TekDeque_push_back(deque, elmt, elmt_size, elmt_align) _TekDeque_push_back_many((_TekDeque*)deque, elmt, 1, elmt_size, elmt_align)
#define TekDeque_push_back_many(deque, elmts, elmts_count) ({ tek_assert_type((deque)->_TekDeque_data, elmts); _TekDeque_push_back_many((_TekDeque*)deque, elmts, elmts_count, sizeof(*(deque)->_TekDeque_data), alignof(typeof(*(deque)->_TekDeque_data))); })
extern void _TekDeque_push_back_many(_TekDeque* deque, void* elmts, uint32_t elmts_count, uint32_t elmt_size, uint32_t elmt_align);

// removes element/s from the front of the deque
// returns the number of elements popped from the deque.
// elements will be copied to elmts_out unless it is NULL.
#define TekDeque_pop_front(deque, elmt_out) ({ tek_assert_type((deque)->_TekDeque_data, elmt_out); _TekDeque_pop_front_many((_TekDeque*)deque, elmt_out, 1, sizeof(*(deque)->_TekDeque_data)); })
#define _TekDeque_pop_front(deque, elmt_out, elmt_size) _TekDeque_pop_front_many((_TekDeque*)deque, elmt_out, 1, elmt_size)
#define TekDeque_pop_front_many(deque, elmts_out, elmts_count) ({ tek_assert_type((deque)->_TekDeque_data, elmts_out); _TekDeque_pop_front_many((_TekDeque*)deque, elmts_out, elmts_count, sizeof(*(deque)->_TekDeque_data)); })
extern uint32_t _TekDeque_pop_front_many(_TekDeque* deque, void* elmts_out, uint32_t elmts_count, uint32_t elmt_size);

// removes element/s from the back of the deque
// returns the number of elements popped from the deque.
// elements will be copied to elmts_out unless it is NULL.
#define TekDeque_pop_back(deque, elmt_out) ({ tek_assert_type((deque)->_TekDeque_data, elmt_out); _TekDeque_pop_back_many((_TekDeque*)deque, elmt_out, 1, sizeof(*(deque)->_TekDeque_data)); })
#define _TekDeque_pop_back(deque, elmt_out, elmt_size) _TekDeque_pop_back_many((_TekDeque*)deque, elmt_out, 1, elmt_size)
#define TekDeque_pop_back_many(deque, elmts_out, elmts_count) ({ tek_assert_type((deque)->_TekDeque_data, elmts_out); _TekDeque_pop_back_many((_TekDeque*)deque, elmts_out, elmts_count, sizeof(*(deque)->_TekDeque_data)); })
extern uint32_t _TekDeque_pop_back_many(_TekDeque* deque, void* elmts_out, uint32_t elmts_count, uint32_t elmt_size);

//===========================================================================================
//
//
// memory allocation - element pool
//
//
//===========================================================================================

typedef uint32_t TekPoolId;

typedef struct _TekPool _TekPool;
struct _TekPool {
	/*
	uint8_t is_allocated_bitset[(cap / 8) + 1]
	T elements[cap]
	*/
	void* data;
	uint32_t elmts_start_byte_idx;
	uint32_t count;
	uint32_t cap;
	uint32_t free_list_head_id;
};

#define TekPool(T) TekPool_##T

#define typedef_TekPool(T) \
typedef struct { \
	T* TekPool_data; \
	uint32_t elmts_start_byte_idx; \
	uint32_t count; \
	uint32_t cap; \
	uint32_t free_list_head_id; \
} TekPool_##T;

#define TekPool_reset(pool) _TekPool_reset((_TekPool*)pool, sizeof(*(pool)->TekPool_data))
void _TekPool_reset(_TekPool* pool, uintptr_t elmt_size);

#define TekPool_expand(pool, new_cap) _TekPool_expand((_TekPool*)pool, new_cap, sizeof(*(pool)->TekPool_data), alignof(typeof(*(pool)->TekPool_data)))
void _TekPool_expand(_TekPool* pool, uint32_t new_cap, uintptr_t elmt_size, uintptr_t elmt_align);

#define TekPool_reset_and_populate(pool, elmts, count) _TekPool_reset_and_populate((_TekPool*)pool, elmts, count, sizeof(*(pool)->TekPool_data), alignof(typeof(*(pool)->TekPool_data)))
void _TekPool_reset_and_populate(_TekPool* pool, void* elmts, uint32_t count, uintptr_t elmt_size, uintptr_t elmt_align);

#define TekPool_init(pool, cap) _TekPool_init((_TekPool*)pool, cap, sizeof(*(pool)->TekPool_data), alignof(typeof(*(pool)->TekPool_data)))
void _TekPool_init(_TekPool* pool, uint32_t cap, uintptr_t elmt_size, uintptr_t elmt_align);

#define TekPool_deinit(pool) _TekPool_deinit((_TekPool*)pool, sizeof(*(pool)->TekPool_data), alignof(typeof(*(pool)->TekPool_data)))
void _TekPool_deinit(_TekPool* pool, uintptr_t elmt_size, uintptr_t elmt_align);

#define TekPool_alloc(pool, id_out) (typeof((pool)->TekPool_data))_TekPool_alloc((_TekPool*)pool, id_out, sizeof(*(pool)->TekPool_data), alignof(typeof(*(pool)->TekPool_data)))
void* _TekPool_alloc(_TekPool* pool, TekPoolId* id_out, uintptr_t elmt_size, uintptr_t elmt_align);

#define TekPool_dealloc(pool, elmt_id) _TekPool_dealloc((_TekPool*)pool, elmt_id, sizeof(*(pool)->TekPool_data), alignof(typeof(*(pool)->TekPool_data)))
void _TekPool_dealloc(_TekPool* pool, TekPoolId elmt_id, uintptr_t elmt_size, uintptr_t elmt_align);

#define TekPool_id_to_ptr(pool, elmt_id) (typeof((pool)->TekPool_data))_TekPool_id_to_ptr((_TekPool*)pool, elmt_id, sizeof(*(pool)->TekPool_data))
void* _TekPool_id_to_ptr(_TekPool* pool, TekPoolId elmt_id, uintptr_t elmt_size);

#define TekPool_ptr_to_id(pool, ptr) _TekPool_ptr_to_id((_TekPool*)pool, ptr, sizeof(*(pool)->TekPool_data))
TekPoolId _TekPool_ptr_to_id(_TekPool* pool, void* ptr, uintptr_t elmt_size);

//===========================================================================================
//
//
// memory allocation - key value stack
//
//
//===========================================================================================

typedef struct _TekKVStk _TekKVStk;
struct _TekKVStk {
	/*
	K keys[cap]
	V values[cap]
	*/
	void* data;
	uint32_t count;
	uint32_t cap;
};

#define TekKVStk(K, V) TekKVStk_##K##_##V
#define typedef_TekKVStk(K, V) \
	typedef struct { \
		/* HACK: not the actual represention, these types here to be used by the macros */ \
		struct { K k; V v; }* TekKVStk_data; \
		uint32_t count; \
		uint32_t cap; \
	} TekKVStk_##K##_##V;

#define TekKVStk_init(kv_stk, cap) _TekKVStk_init((_TekKVStk*)kv_stk, cap, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof(*(kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v)))
void _TekKVStk_init(_TekKVStk* kv_stk, uint32_t cap, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

#define TekKVStk_deinit(kv_stk) _TekKVStk_deinit((_TekKVStk*)kv_stk, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof((kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v)))
void _TekKVStk_deinit(_TekKVStk* kv_stk, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

#define TekKVStk_get_key(kv_stk, idx) (typeof(&(kv_stk)->TekKVStk_data->k))_TekKVStk_get_key((_TekKVStk*)kv_stk, idx, sizeof((kv_stk)->TekKVStk_data->k))
void* _TekKVStk_get_key(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size);

#define TekKVStk_get_value(kv_stk, idx) (typeof(&(kv_stk)->TekKVStk_data->v))_TekKVStk_get_value((_TekKVStk*)kv_stk, idx, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof((kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v)))
void* _TekKVStk_get_value(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

// @return: an identifier which is + 1 of the key's index and 0 means null
uint32_t _TekKVStk_find_key_32(_TekKVStk* kv_stk, uint32_t start_idx, uint32_t end_idx, uint32_t key, uint32_t key_size);
uint32_t _TekKVStk_find_key_64(_TekKVStk* kv_stk, uint32_t start_idx, uint32_t end_idx, uint64_t key, uint32_t key_size);

#define TekKVStk_find_key_32(kv_stk, start_idx, end_idx, key) \
	_TekKVStk_find_key_32((_TekKVStk*)kv_stk, start_idx, end_idx, key, sizeof((kv_stk)->TekKVStk_data->k))

#define TekKVStk_find_key_64(kv_stk, start_idx, end_idx, key) \
	_TekKVStk_find_key_64((_TekKVStk*)kv_stk, start_idx, end_idx, key, sizeof((kv_stk)->TekKVStk_data->k))

#if TEK_HASH_64
#define TekKVStk_find_key_hash TekKVStk_find_key_64
#else
#define TekKVStk_find_key_hash TekKVStk_find_key_32
#endif

#define TekKVStk_find_key_str_id TekKVStk_find_key_32

#define TekKVStk_resize_cap(kv_stk, new_cap) _TekKVStk_resize_cap((_TekKVStk*)kv_stk, new_cap, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof((kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v)))
void _TekKVStk_resize_cap(_TekKVStk* kv_stk, uint32_t new_cap, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

#define TekKVStk_push(kv_stk, key, value) { tek_assert_type(&(kv_stk)->TekKVStk_data->k, key); tek_assert_type(&(kv_stk)->TekKVStk_data->v, value); _TekKVStk_push((_TekKVStk*)kv_stk, key, value, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof((kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v))); }
void _TekKVStk_push(_TekKVStk* kv_stk, void* key, void* value, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

#define TekKVStk_insert(kv_stk, idx, key, value) { tek_assert_type(&(kv_stk)->TekKVStk_data->k, key); tek_assert_type(&(kv_stk)->TekKVStk_data->v, value); (typeof((kv_stk)->TekKVStk_data->k))_TekKVStk_insert((_TekKVStk*)kv_stk, idx, key, value, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof((kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v))) }
void _TekKVStk_insert(_TekKVStk* kv_stk, uint32_t idx, void* key, void* value, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

#define TekKVStk_remove_swap(kv_stk, idx) (typeof((kv_stk)->TekKVStk_data->k))_TekKVStk_remove_swap((_TekKVStk*)kv_stk, idx, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof((kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v)))
void _TekKVStk_remove_swap(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

#define TekKVStk_remove_shift(kv_stk, idx) (typeof((kv_stk)->TekKVStk_data->k))_TekKVStk_remove_shift((_TekKVStk*)kv_stk, idx, sizeof((kv_stk)->TekKVStk_data->k), alignof(typeof((kv_stk)->TekKVStk_data->k)), sizeof((kv_stk)->TekKVStk_data->v), alignof(typeof((kv_stk)->TekKVStk_data->v)))
void _TekKVStk_remove_shift(_TekKVStk* kv_stk, uint32_t idx, uint32_t key_size, uint32_t key_align, uint32_t value_size, uint32_t value_align);

//===========================================================================================
//
//
// memory allocation - string table
//
//
//===========================================================================================

#if TEK_HASH_64
typedef uint64_t TekHash;
#else
typedef uint32_t TekHash;
#endif

typedef uint32_t TekStrId;

typedef char* TekStrEntry;
static inline uint32_t TekStrEntry_len(TekStrEntry str_entry) { return *(uint32_t*)str_entry; }
static inline char* TekStrEntry_value(TekStrEntry str_entry) { return tek_ptr_add(str_entry, sizeof(uint32_t)); }

TekHash tek_hash_fnv(char* bytes, uint32_t byte_count, TekHash hash);

typedef_TekKVStk(TekHash, TekStrEntry);
typedef TekKVStk(TekHash, TekStrEntry) TekStrTab;

static inline void TekStrTab_init(TekStrTab* strtab, uint32_t cap) {
	TekKVStk_init(strtab, cap);
}

static inline void TekStrTab_deinit(TekStrTab* strtab) {
	TekKVStk_deinit(strtab);
}

TekStrId TekStrTab_get_or_insert(TekStrTab* strtab, char* str, uint32_t str_len);

static inline TekStrEntry TekStrTab_get_entry(TekStrTab* strtab, TekStrId str_id) {
	tek_assert(str_id, "cannot get a string with a NULL TekStrId");
	return *TekKVStk_get_value(strtab, str_id - 1);
}

//===========================================================================================
//
//
// memory allocation - virtual memory
//
//
//===========================================================================================


typedef uint8_t TekVirtMemProtection;
enum {
	TekVirtMemProtection_no_access,
	TekVirtMemProtection_read,
	TekVirtMemProtection_read_write,
	TekVirtMemProtection_exec_read,
	TekVirtMemProtection_exec_read_write,
};

//
// 0 means success
typedef uint32_t TekVirtMemError;

//
// @return: the previous error of a virtual memory function call.
//          only call this directly after one of the virtual memory functions.
//          on Unix: this returns errno
//          on Windows: this returns GetLastError()
//
TekVirtMemError tek_virt_mem_get_last_error();

typedef uint8_t TekVirtMemErrorStrRes;
enum {
	TekVirtMemErrorStrRes_success,
	TekVirtMemErrorStrRes_invalid_error_arg,
	TekVirtMemErrorStrRes_not_enough_space_in_buffer,
};

//
// get the string of @param(error) and writes it into the @param(buf_out) pointer upto @param(buf_out_len)
// @param(buf_out) is null terminated on success.
//
// @return: the result detailing the success or error.
//
TekVirtMemErrorStrRes tek_virt_mem_get_error_string(TekVirtMemError error, char* buf_out, uint32_t buf_out_len);

//
// @return: the page size of the OS.
// 			used to align the parameters of the virtual memory functions to a page.
//          On Windows this actually returns the page granularity and not the page size.
//          since Virtual{Alloc, Protect, Free}.lpAddress must be aligned to the page granularity (region of pages)
//
uintptr_t tek_virt_mem_page_size();

//
// reserve a range of memory in the virtual address space
// reading or writing to this memory for the first time will be commited on a page by page basis.
// once commit, the memory will be zero.
//
// @param requested_addr: the requested address you wish to reserve.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//             this is not guaranteed and is only used as hint.
//             NULL will not be used as a hint.
//
// @param size: the size in bytes you wish to reserve from the @param(requested_addr)
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
// @param protection: what the memory is allowed to be used for
//
// @return: NULL on error, otherwise the start of the reserved block of memory.
//          if errored you can get the error by calling tek_virt_mem_get_last_error()
//          directly after this call
//
void* tek_virt_mem_reserve(void* requested_addr, uintptr_t size, TekVirtMemProtection protection);

//
// change the protection of a range of memory.
// this memory must have been reserved with tek_virt_mem_reserve.
//
// @param addr: the start of the pages you wish to change the protection for.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
// @param size: the size in bytes of the memory you wish to change protection for.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
// @param protection: what the memory is allowed to be used for
//
TekBool tek_virt_mem_protection_set(void* addr, uintptr_t size, TekVirtMemProtection protection);

//
// gives the memory back to the OS but will keep the address space reserved
//
// @param addr: the start of the pages you wish to decommit.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
// @param size: the size in bytes of the memory you wish to release.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
TekBool tek_virt_mem_decommit(void* addr, uintptr_t size);

//
// gives the pages reserved back to the OS. the address range must have be reserved with tek_virt_mem_reserve.
// you can target sub pages of the original allocation but just make sure the parameters are aligned.
//
// @param addr: the start of the pages you wish to release.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
// @param size: the size in bytes of the memory you wish to release.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
TekBool tek_virt_mem_release(void* addr, uintptr_t size);

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
typedef int TekVirtMemFileHandle;
#else
#error "TODO implement virtual memory for this platform"
#endif

//
// maps a file at the path @param(path) into memory.
// you must use tek_virt_mem_release before calling
// tek_virt_mem_map_file_close when you are finished with the memory mapped file.
//
//
// @param path: the start of the pages you wish to release.
//             must be a aligned to whatever tek_virt_mem_page_size returns.
//
// @param protection: what the memory is allowed to be used for
//
// @param size_out: apon success, this parameter will be set to the size of the the file
//
// @param file_handle_out: apon success, this parameter will be set to the file handle
//                         that you can use to close the file with.
//
// @return: NULL on failure, otherwise the address pointing to the start of the memory mapped file is returned.
//          if errored you can get the error by calling tek_virt_mem_get_last_error() directly after this call.
//
void* tek_virt_mem_map_file(char* path, TekVirtMemProtection protection, uintptr_t* size_out, TekVirtMemFileHandle* file_handle_out);

//
// closes the file that was mapped with tek_virt_mem_map_file
//
// @param file_handle: the handle of the file to close.
//
//
// @return: tek_false on failure, otherwise tek_true is returned.
//          if errored you can get the error by calling tek_virt_mem_get_last_error() directly after this call.
//
TekBool tek_virt_mem_map_file_close(TekVirtMemFileHandle file_handle);

//===========================================================================================
//
//
// memory allocation - arena allocator
//
//
//===========================================================================================

typedef struct TekLinearAlctor TekLinearAlctor;

struct TekLinearAlctor {
	void* data;
	void* pos;
};

TekVirtMemError TekLinearAlctor_init(TekLinearAlctor* alctor);
void TekLinearAlctor_deinit(TekLinearAlctor* alctor);
void TekLinearAlctor_reset(TekLinearAlctor* alctor);

//
// @return: a pointer to memory with size and align that is zeroed.
void* TekLinearAlctor_alloc(TekLinearAlctor* alctor, uintptr_t size, uintptr_t align);

void* TekLinearAlctor_TekAlctor_fn(void* alctor_data, void* ptr, uintptr_t old_size, uintptr_t size, uintptr_t align);

//===========================================================================================
//
//
// Threading
//
//
//===========================================================================================

#if defined(__x86_64__) || defined(__i386__)
#define tek_cpu_relax() __asm__ __volatile__("pause")
#elif defined(__aarch64__) || defined(__arm__)
#define tek_cpu_relax() __asm__ __volatile__("yield")
#else
#define tek_cpu_relax()
#endif

//
// Mutex
//
typedef struct {
    _Atomic(int) _locked;
} TekMtx;

#define TekMtx_init() (TekMtx){ ._locked = false }

// returns when the mtx has been successfully locked
void TekMtx_lock(TekMtx* mtx);
// unlocks the mtx so other threads waiting on the lock can lock it
void TekMtx_unlock(TekMtx* mtx);

typedef struct {
    _Atomic(TekBool) _locked;
} TekSpinMtx;

void TekSpinMtx_lock(TekSpinMtx* mtx);
void TekSpinMtx_unlock(TekSpinMtx* mtx);

//
// Read Write lock
// allows for multiple threads to lock a resource for read access.
// a lock for write access will ensure there are no threads that have read access until the write access is returned.
//
// this implementation favours write access, so as soon as another thread request write access
// the lock will not allow any reads until write access has been returned
//
typedef struct {
    _Atomic uint16_t _read_access_count;
    _Atomic uint16_t _write_access_count;
} TekSpinRWLock;

#define TekSpinRWLock_init() (TekSpinRWLock){ ._read_access_count = 0, ._write_access_count = 0 }

// returns when the lock has been successfully locked for read access
// this will wait if write access is being requested or held by another thread
void TekSpinRWLock_lock_for_read(TekSpinRWLock* lock);

// returns the read access back to the lock
void TekSpinRWLock_unlock_for_read(TekSpinRWLock* lock);

// returns when the lock has been successfully locked for write access
// this will wait if read or write access is being held by another thread
void TekSpinRWLock_lock_for_write(TekSpinRWLock* lock);

// returns the write access back to the lock
void TekSpinRWLock_unlock_for_write(TekSpinRWLock* lock);

uint32_t tek_atomic_find_key_32(_Atomic uint32_t* keys, uint32_t key, uint32_t start_idx, uint32_t end_idx);
uint32_t tek_atomic_find_key_64(_Atomic uint64_t* keys, uint64_t key, uint32_t start_idx, uint32_t end_idx);
#define tek_atomic_find_str_id tek_atomic_find_key_32
#if TEK_HASH_64
#define tek_atomic_find_hash tek_atomic_find_key_64
#else
#define tek_atomic_find_hash tek_atomic_find_key_32
#endif

//===========================================================================================
//
//
// file system
//
//
//===========================================================================================

// @return: 0 on success, otherwise the value in "errno" is returned
int tek_file_exist(char* file_path);

//
// resolves any symbolic links and removes any "../" and "..\"
// @return: 0 on success, otherwise the value in "errno" is returned
int tek_file_path_normalize_resolve(char* path, char* path_buf_out);

// @return: 0 on success, otherwise the value in "errno" is returned
int tek_file_read(char* path, TekStk(char)* bytes_out);

// @return: 0 on success, otherwise the value in "errno" is returned
int tek_file_write(char* path, void* data, uintptr_t size);

#endif

