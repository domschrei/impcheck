
cmake_minimum_required(VERSION 3.11.4)
project (impcheck_modules)

add_definitions("-std=gnu99 -Wall -Wextra -Werror -pedantic-errors -flto -g")
if(IMPCHECK_WRITE_DIRECTIVES)
    add_definitions("-DIMPCHECK_WRITE_DIRECTIVES=${IMPCHECK_WRITE_DIRECTIVES}")
endif()
if(IMPCHECK_FLUSH_ALWAYS)
    add_definitions("-DIMPCHECK_FLUSH_ALWAYS=${IMPCHECK_FLUSH_ALWAYS}")
endif()

set(src_parse "src/trusted/trusted_parser.c")
if(IMPCHECK_COMPRESS EQUAL 1)
    add_definitions("-DIMPCHECK_COMPRESS=1")
endif()
if(IMPCHECK_COMPRESS EQUAL 2)
    add_definitions("-DIMPCHECK_COMPRESS=1")
    set(src_parse "src/trusted/trusted_compressing_parser.c")
endif()

execute_process(COMMAND bash "-c" "dd if=/dev/urandom count=8 bs=1 2>/dev/null | od -t u8 | awk '{print $2\"UL\"}' | head -1" OUTPUT_VARIABLE IMPCHECK_SALT)
file(WRITE src/trusted/salt.h "#define IMPCHECK_SALT " ${IMPCHECK_SALT})

add_executable(impcheck_parse
    src/writer.c
    src/trusted/clausecompress.c
    src/trusted/sort.c
    src/trusted/confirm.c
    src/trusted/hash.c
    src/trusted/keygen.c
    src/trusted/secret.c
    src/trusted/siphash.c
    src/trusted/trusted_utils.c
    src/trusted/vectors.c
    ${src_parse}
    src/trusted/trusted_parser_main.c
    src/trusted/signature_trace.c
    src/trusted/main_parse.c
)
add_executable(impcheck_check 
    src/writer.c
    src/trusted/clausecompress.c
    src/trusted/sort.c
    src/trusted/confirm.c
    src/trusted/hash.c
    src/trusted/keygen.c
    src/trusted/lrat_check.c
    src/trusted/pointer_storage.c
    src/trusted/secret.c
    src/trusted/siphash.c
    src/trusted/top_check.c
    src/trusted/trusted_checker.c
    src/trusted/trusted_utils.c
    src/trusted/vectors.c
    src/trusted/main_check.c
)
add_executable(impcheck_confirm
    src/writer.c
    src/trusted/clausecompress.c
    src/trusted/confirm.c
    src/trusted/hash.c
    src/trusted/keygen.c
    src/trusted/secret.c
    src/trusted/siphash.c
    src/trusted/sort.c
    src/trusted/trusted_utils.c
    src/trusted/vectors.c
    ${src_parse}
    src/trusted/trusted_parser_main.c
    src/trusted/signature_trace.c
    src/trusted/main_confirm.c
)

add_executable(test_hash src/trusted/trusted_utils.c src/trusted/hash.c src/writer.c test/test.c
    test/test_hash.c)
add_executable(test_sig src/trusted/trusted_utils.c src/trusted/hash.c src/trusted/siphash.c src/trusted/secret.c src/writer.c test/test.c
    test/test_sig.c)
add_executable(test_full src/trusted/trusted_utils.c src/writer.c test/test.c src/trusted/vectors.c
    test/test_full.c)
