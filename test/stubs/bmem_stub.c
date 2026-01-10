#include <stdlib.h>
#include <string.h>

void *bzalloc(size_t size) {
	void *ptr = malloc(size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void bfree(void *ptr) {
	free(ptr);
}
