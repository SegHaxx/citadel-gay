/*
 * This is a quickly-hacked-together implementation of an elastic array class.  It includes constructor and destructor
 * methods, append and access methods, and that's about it.  The memory allocation is very naive: whenever we are about
 * to append beyond the size of the buffer, we double its size.
 *
 * Copyright (c) 2021 by Art Cancro
 *
 * This program is open source software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
	}

	++arr->num_elements;
	if (arr->num_elements > arr->num_alloc) {
		arr->num_alloc = arr->num_alloc * 2;
		arr->the_elements = realloc(arr->the_elements, (arr->element_size * arr->num_alloc));
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
