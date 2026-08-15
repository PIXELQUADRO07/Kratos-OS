/* kratos-sign.h — KratosOS Cryptographic Signature Verification (Ed25519)
 *
 * Utilizza mbedTLS per verificare firme digitali di indici e pacchetti.
 */

#ifndef KRATOS_SIGN_H
#define KRATOS_SIGN_H

#include <stddef.h>

/*
 * Verifica la firma di un file.
 * Legge il file, legge la firma in formato esadecimale (o binary se specificato),
 * e la valida usando la chiave pubblica.
 *
 * Ritorna 0 se la firma è valida, -1 altrimenti.
 */
int kratos_verify_file(const char *file_path, const char *sig_path, const char *pubkey_path);

/*
 * Verifica la firma Ed25519 su un buffer in memoria.
 *
 * Ritorna 0 se valida, -1 altrimenti.
 */
int kratos_verify_buffer(const unsigned char *data, size_t data_len,
                         const unsigned char *sig, size_t sig_len,
                         const char *pubkey_path);

/*
 * Firma un buffer in memoria usando una chiave privata.
 * Scrive la firma generata in sig_out (formato raw binario).
 * Ritorna 0 se ha successo, -1 altrimenti.
 */
int kratos_sign_buffer(const unsigned char *data, size_t data_len,
                       unsigned char *sig_out, size_t *sig_len_out,
                       const char *privkey_path);

#endif /* KRATOS_SIGN_H */
