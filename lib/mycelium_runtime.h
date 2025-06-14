#pragma once

#include "platform.hpp" // Include for MYCELIUM_API macro
#include <stddef.h> // For size_t
#include <stdbool.h> // For bool type in C
#include <stdint.h>  // For int32_t, uint32_t
#include <common/logger.hpp>

#ifdef __cplusplus
#include <atomic>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Forward Declarations ---
struct MyceliumVTable;

// --- Object Header for ARC ---
// NOTE: This header precedes every managed object in memory.
// Layout: [MyceliumObjectHeader][Object's actual data fields]
typedef struct {
#ifdef __cplusplus
    std::atomic<int32_t> ref_count; // Thread-safe reference count for ARC
#else
    volatile int32_t ref_count;      // C fallback - mark as volatile for basic thread awareness
#endif
    uint32_t type_id;                // Simple type identifier  
    struct MyceliumVTable* vtable;   // Pointer to virtual method table
} MyceliumObjectHeader;

// --- Virtual Method Table Structure ---
// Each class type has a vtable containing function pointers for virtual methods
typedef struct MyceliumVTable {
    void (*destructor)(void* obj_fields_ptr);  // Destructor function pointer (required)
    // Future: Virtual method pointers will be added here
} MyceliumVTable;

// --- MyceliumString Structure (Existing) ---
typedef struct {
    char* data;      // Null-terminated character array
    size_t length;   // Number of characters (excluding null terminator)
    size_t capacity; // Allocated buffer size (data buffer size, including space for null terminator)
} MyceliumString;

// --- ARC Runtime Function Declarations ---
MYCELIUM_API MyceliumObjectHeader* Mycelium_Object_alloc(size_t data_size, uint32_t type_id, struct MyceliumVTable* vtable);
MYCELIUM_API void Mycelium_Object_retain(MyceliumObjectHeader* obj_header);
MYCELIUM_API void Mycelium_Object_release(MyceliumObjectHeader* obj_header);
MYCELIUM_API int32_t Mycelium_Object_get_ref_count(MyceliumObjectHeader* obj_header); // For debugging

// --- Thread-safe atomic helpers (C++ only) ---
#ifdef __cplusplus
// These functions provide safe atomic operations for reference counting
int32_t Mycelium_Object_atomic_increment(MyceliumObjectHeader* obj_header);
int32_t Mycelium_Object_atomic_decrement(MyceliumObjectHeader* obj_header);
int32_t Mycelium_Object_atomic_load(MyceliumObjectHeader* obj_header);
void Mycelium_Object_atomic_store(MyceliumObjectHeader* obj_header, int32_t value);
#endif

// --- VTable Registry Functions ---
MYCELIUM_API void Mycelium_VTable_register(uint32_t type_id, struct MyceliumVTable* vtable);
MYCELIUM_API struct MyceliumVTable* Mycelium_VTable_get(uint32_t type_id);

// --- MyceliumString Runtime Function Declarations (Existing) ---

// Creates a new MyceliumString from a C string literal.
// The runtime takes ownership of the new string's memory.
MYCELIUM_API MyceliumString* Mycelium_String_new_from_literal(const char* c_str, size_t len);

// Concatenates two MyceliumStrings, returning a new MyceliumString.
// The runtime takes ownership of the new string's memory. s1 and s2 are not modified.
MYCELIUM_API MyceliumString* Mycelium_String_concat(MyceliumString* s1, MyceliumString* s2);

// Prints a MyceliumString to standard output (example utility).
MYCELIUM_API void Mycelium_String_print(MyceliumString* str);

// Deallocates a MyceliumString (important for memory management).
MYCELIUM_API void Mycelium_String_delete(MyceliumString* str);

// --- String Conversion Functions ---

// Convert primitive types to MyceliumString
MYCELIUM_API MyceliumString* Mycelium_String_from_int(int val);
MYCELIUM_API MyceliumString* Mycelium_String_from_long(long long val);
MYCELIUM_API MyceliumString* Mycelium_String_from_float(float val);
MYCELIUM_API MyceliumString* Mycelium_String_from_double(double val);
MYCELIUM_API MyceliumString* Mycelium_String_from_bool(bool val);
MYCELIUM_API MyceliumString* Mycelium_String_from_char(char val);


// Convert MyceliumString to primitive types
// Note: These functions should define behavior for invalid conversions (e.g., return 0, false, or handle errors).
MYCELIUM_API int Mycelium_String_to_int(MyceliumString* str);
MYCELIUM_API long long Mycelium_String_to_long(MyceliumString* str);
MYCELIUM_API float Mycelium_String_to_float(MyceliumString* str);
MYCELIUM_API double Mycelium_String_to_double(MyceliumString* str);
MYCELIUM_API bool Mycelium_String_to_bool(MyceliumString* str); // e.g., "true" -> true, others -> false
MYCELIUM_API char Mycelium_String_to_char(MyceliumString* str); // e.g., takes the first character, or 0 if empty/error

// --- Additional String Functions for Primitive Struct Support ---

// Get the length of a MyceliumString (for string.Length property)
MYCELIUM_API int Mycelium_String_get_length(MyceliumString* str);

// Get substring from a MyceliumString starting at the given index
MYCELIUM_API MyceliumString* Mycelium_String_substring(MyceliumString* str, int startIndex);

// Get an empty MyceliumString (for string.Empty static property)
MYCELIUM_API MyceliumString* Mycelium_String_get_empty(void);


#ifdef __cplusplus
} // extern "C"
#endif