/*
 * This is a quickly-hacked-together implementation of an elastic array class.  It includes constructor and destructor
 * methods, append and access methods, and that's about it.  The memory allocation is very naive: whenever we are about
 * to append beyond the size of the buffer, we double its size.
 *
 * Copyright (c) 2021 by Art Cancro
 *
 * This program is open source software.  Use, duplication, or disclosure
 * is subject to the terms of the GNU General Public License, version 3.
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "libcitadel.h"

/*
 * Constructor for elastic array
 */
Array *array_new(size_t element_size) {
	Array *newarr = malloc(sizeof(Array));
	if (newarr) {
		memset(newarr, 0, sizeof(Array));
	}
	newarr->element_size = element_size;
	return newarr;
}


/*
 * Destructor for elastic array
 */
void array_free(Array *arr) {
	free(arr->the_elements);
	free(arr);
}


/*
 * Append an element to an array (we already know the element size because it's in the data type)
 */
void array_append(Array *arr, void *new_element) {

	if (!arr) {						// silently fail if they gave us a null array
		return;
	}

	if ( (arr->the_elements == NULL) || (arr->num_alloc == 0)) {
		arr->num_alloc = 1;
		arr->num_elements = 0;
		arr->the_elements = malloc(arr->element_size * arr->num_alloc);
		if (arr->the_elements == NULL) {
			abort();
		}
	}

	++arr->num_elements;
	if (arr->num_elements > arr->num_alloc) {
		arr->num_alloc = arr->num_alloc * 2;		// whenever we exceed the buffer size, we double it.
		arr->the_elements = realloc(arr->the_elements, (arr->element_size * arr->num_alloc));
		if (arr->the_elements == NULL) {
			abort();
		}
	}

	memcpy((arr->the_elements + ( (arr->num_elements-1) * arr->element_size )), new_element, arr->element_size);
}


/*
 * Return the element in an array at the specified index
 */
void *array_get_element_at(Array *arr, int index) {
	return (arr->the_elements + ( index * arr->element_size ));
}


/*
 * Return the number of elements in an array
 */
int array_len(Array *arr) {
	return arr->num_elements;
}


/*
 * Sort an array.  We already know the element size and number of elements, so all we need is
 * a sort function, which we will pass along to qsort().
 */
void array_sort(Array *arr, int (*compar)(const void *, const void *)) {
	qsort(arr->the_elements, arr->num_elements, arr->element_size, compar);
}


/*
 * Delete an element from an array
 */
void array_delete_element_at(Array *arr, int index) {

	if (index >= arr->num_elements) {		// If the supplied index is out of bounds, return quietly.
		return;
	}

	if (index < 0) {				// If the supplied index is invalid, return quietly.
		return;
	}

	--arr->num_elements;

	if (index == arr->num_elements) {		// If we deleted the element at the end, there's no more to be done.
		return;
	}

	memcpy(
		(arr->the_elements + (index * arr->element_size)),
		arr->the_elements + ((index+1) * arr->element_size),
		(arr->num_elements - index) * arr->element_size
	);
}
