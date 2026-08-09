/* test-crypt.c — Test end-to-end del KratosOS SHA-512crypt engine */
#include <stdio.h>
#include <string.h>
#include "../init/kratos-crypt.h"

int main(void)
{
    /* Test 1: Genera salt e hash da password "toor" */
    char salt[64];
    kratos_gensalt(salt, sizeof(salt));
    printf("Salt generato: %s\n", salt);

    const char *password = "toor";
    /* Copia l'hash in un buffer statico: kratos_crypt usa static buffer! */
    char hash[200];
    strncpy(hash, kratos_crypt(password, salt), sizeof(hash) - 1);
    printf("Hash SHA-512:  %s\n\n", hash);

    /* Test 2: Stessa password + stored hash → stesso hash */
    char *hash2 = kratos_crypt(password, hash);
    int ok = strcmp(hash, hash2) == 0;
    printf("Verifica hash: %s\n", ok ? "[PASS] hash corrisponde" : "[FAIL] hash non corrisponde");

    /* Test 3: Password errata → hash diverso */
    char *hash3 = kratos_crypt("wrongpassword", hash);
    int fail_ok = strcmp(hash, hash3) != 0;
    printf("Password errata: %s\n", fail_ok ? "[PASS] hash diverso" : "[FAIL] hash uguale — BUG!");

    /* Test 4: Password vuota */
    char *hash4 = kratos_crypt("", salt);
    printf("Password vuota: %s\n", (hash4 && strlen(hash4) > 3) ? "[PASS] hash generato" : "[FAIL] nessun hash");

    /* Test 5: Cross-check con pyth crypt via known vector */
    /* Known SHA-512crypt test vector (from Drepper 2007 spec):
     * password: "Hello world!"
     * salt:     "saltstring"
     * expected hash starts with $6$saltstring$ */
    char *known = kratos_crypt("Hello world!", "$6$saltstring$");
    int known_prefix = strncmp(known, "$6$saltstring$", 14) == 0;
    printf("Vettore noto:  %s\n", known_prefix ? "[PASS] prefisso corretto" : "[FAIL] prefisso errato");
    printf("  => %s\n", known);

    return (ok && fail_ok) ? 0 : 1;
}
