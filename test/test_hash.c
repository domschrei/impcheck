
#include "test.h"
#include "../src/trusted/hash.h"

void test_small() {

    struct hash_table* ht = hash_table_init(7);
    do_assert(ht->size == 0);

    bool ok;
    int arr[128];
    for (u64 i = 0; i < 63; i++) {
        ok = hash_table_insert(ht, i, arr+i);
        do_assert(ok);
    }
    do_assert(ht->size == 63);
    do_assert(ht->capacity == 128);

    for (u64 i = 0; i < 63; i++) {
        do_assert(hash_table_find(ht, i) == arr+i);
    }

    ok = hash_table_insert(ht, 63, arr+63);
    do_assert(ok);
    do_assert(ht->size == 64);
    do_assert(ht->capacity == 128);

    ok = hash_table_insert(ht, 64, arr+64);
    do_assert(ok);
    printf("%lu\n", ht->size);
    do_assert(ht->size == 65);
    do_assert(ht->capacity == 256);

    for (u64 i = 0; i < 65; i++) {
        if (i == 40) continue;
        do_assert(hash_table_find(ht, i) == arr+i);
        do_assert(hash_table_delete(ht, i));
        do_assert(hash_table_find(ht, i) == 0);
    }
    do_assert(ht->size == 1);
    do_assert(hash_table_find(ht, 40) == arr+40);
    do_assert(hash_table_delete_last_found(ht));
    do_assert(ht->size == 0);
    do_assert(hash_table_find(ht, 40) == 0);

    hash_table_free(ht);
}

void test_big() {
    struct hash_table* ht = hash_table_init(7);
    do_assert(ht->size == 0);

    bool ok;
    int obj;

    u64 nb_elems = (1<<20)+3;
    for (u64 i = 0; i < nb_elems; i++) {
        do_assert(!hash_table_find(ht, i));
        ok = hash_table_insert(ht, i, (&obj)+i);
        do_assert(ok);
        void* val = hash_table_find(ht, i);
        do_assert(val != 0);
        do_assert(val == (&obj)+i);
    }
    do_assert(ht->size == nb_elems);

    for (u64 i = 0; i < nb_elems; i++) {
        do_assert(ht->size == nb_elems - i);
        void* val = hash_table_find(ht, i);
        do_assert(val != 0);
        do_assert(val == (&obj)+i);
        bool ok = hash_table_delete_last_found(ht);
        do_assert(ok);
        do_assert(!hash_table_find(ht, i));
    }
    do_assert(ht->size == 0);

    hash_table_free(ht);
}

void test_alternate() {
    struct hash_table* ht = hash_table_init(7);
    do_assert(ht->size == 0);

    bool ok;
    int obj;

    // In block sizes of b=256, insert b elements and then delete b/2 of them
    // (those with an even key).
    const int block_size = 256;
    const int nb_iterations = 9999;
    int counter = 0;
    for (int outer = 0; outer < nb_iterations; outer++) {
        for (int i = 0; i < block_size; i++) {
            do_assert(!hash_table_find(ht, counter));
            ok = hash_table_insert(ht, counter, (&obj)+counter);
            do_assert(ok);
            void* val = hash_table_find(ht, counter);
            do_assert(val != 0);
            do_assert(val == (&obj)+counter);
            counter++;
        }
        for (int delcounter = counter - block_size; delcounter < counter; delcounter+=2) {
            void* val = hash_table_find(ht, delcounter);
            do_assert(val != 0);
            do_assert(val == (&obj)+delcounter);
            ok = hash_table_delete_last_found(ht);
            do_assert(ok);
            do_assert(!hash_table_find(ht, delcounter));
        }
    }

    // Check that all even keys are deleted and all odd keys are present.
    printf("size=%lu counter=%i\n", ht->size, counter);
    for (int i = 0; i < counter; i++) {
        if (i % 2 == 0) {
            // deleted
            do_assert(!hash_table_find(ht, i));
        } else {
            // present
            void* val = hash_table_find(ht, i);
            do_assert(val != 0);
            do_assert(val == (&obj)+i);
        }
    }
    do_assert((int)ht->size == counter/2);
}

int main() {
    test_small();
    test_big();
    test_alternate();
}
