/* kratos-crypt.h — KratosOS Password Hashing Engine (SHA-512) */

#ifndef KRATOS_CRYPT_H
#define KRATOS_CRYPT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Computes a SHA-512 salted password hash ($6$salt$hash) */
char *kratos_crypt(const char *key, const char *salt);

/* Generates a random SHA-512 salt string ($6$randomsalt$) */
void kratos_gensalt(char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* KRATOS_CRYPT_H */
