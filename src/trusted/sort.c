
#include "sort.h"
#include <stdlib.h>

void insertion_sort(int* arr, int size) {
    for (int i = 1; i < size; i++) {
        int key = arr[i];
        int j = i - 1;
        // Move elements of arr[0..i-1] that are greater than key
        // to one position ahead of their current position
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = key;
    }
}

// sort unsigned integers in increasing order
int qsort_compare(const void* a, const void* b) {
    return *(int*)a - *(int*)b;
}
void sort_ints(int* ints, u32 nb_ints) {
    if (nb_ints <= 128) {
        insertion_sort(ints, nb_ints);
        return;
    }
    qsort(ints, nb_ints, sizeof(int), qsort_compare);
}

void sort_objs(void* objs, u32 nb_objs, u32 bytes_per_obj, int (*compare)(const void *, const void *)) {
    qsort(objs, nb_objs, bytes_per_obj, compare);
}
