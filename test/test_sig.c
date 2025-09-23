
#include "test.h"
#include <stdio.h>

#include "../src/trusted/siphash.h"
#include "../src/trusted/secret.h"

void test_sig_coherent(void) {
    printf("[TEST] --- begin test_sig_coherent() ---\n");

#define N 1024

    int data[N];
    for (int i = 0; i < N; i++) data[i] = i;

    struct siphash* sh = siphash_init(SECRET_KEY);

    // We test reading the N integers of data in chunks of j integers for varying j
    // and that we get the same final signature regardless of j.
    SIG_TYPE endsig;
    for (int j = 1; j <= N; j++) {
        siphash_reset(sh);
        int pos = 0;
        while (pos < N) {
            int nb_ints = j;
            if (pos+nb_ints > N) nb_ints = N-pos;
            siphash_update(sh, ((u8*) data) + pos*sizeof(int), nb_ints * sizeof(int));
            (void) siphash_end_branch(sh, 2);
            pos += nb_ints;
        }
        do_assert(pos == N);
        SIG_TYPE sig = siphash_end_branch(sh, 2);

        char sigstr[33];
        trusted_utils_sig_to_str((u8*) &sig, sigstr);
        printf("j=%i : sig=%s\n", j, sigstr);

        if (j > 1) do_assert(trusted_utils_equal_signatures(endsig, sig));
        endsig = sig;
    }

    printf("[TEST] ---  end  test_sig_coherent() ---\n\n");
}

int main(void) {
    test_sig_coherent();
}
