#ifndef __ALLOCATOR_H__
#define __ALLOCATOR_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

	// malloc()
	extern void *allocator_759E0635(size_t size);

	// free()
	extern void allocator_77A602DD(void *ptr);

	// realloc()
	extern void *allocator_F7A14A22(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#define malloc  allocator_759E0635
#define free    allocator_77A602DD
#define realloc allocator_F7A14A22

#endif // __ALLOCATOR_H__