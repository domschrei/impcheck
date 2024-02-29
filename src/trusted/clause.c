
#include "clause.h"
#include <stdlib.h>  // for calloc, free

int* clause_init(const int* data, int nb_lits) {
    int* cls = (int*) calloc(nb_lits+1, sizeof(int));
    for (int i = 0; i < nb_lits; i++) cls[i] = data[i];
    cls[nb_lits] = 0;
    return cls;
}
void clause_free(int* cls) {
    free(cls);
}
