/*
 * Utility routines. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <memory.h>
#include "mbedtls/base64.h"
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include "common.h"

shairport_cfg config;

int debuglev = 0;

void die(char *format, ...) {
    fprintf(stderr, "FATAL: ");

    va_list args;
    va_start(args, format);

    vfprintf(stderr, format, args);

    va_end(args);

    fprintf(stderr, "\n");
    shairport_shutdown(1);
}

void warn(char *format, ...) {
    fprintf(stderr, "WARNING: ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void debug(int level, char *format, ...) {
    //if (level > debuglev)
    //    return;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

#define CONFIG_MDNS_HOSTNAME   "ESP-WROVER"
#define CONFIG_MDNS_INSTANCE   "ESP with mDNS"

char* generate_hostname(void)
{
    return strdup(CONFIG_MDNS_HOSTNAME);
}
char *base64_enc(uint8_t *input, int length) {
    int bufsize = length*4/3 + 5;
    unsigned char *buf = malloc(bufsize);

    size_t olen;
    mbedtls_base64_encode(buf, bufsize, &olen, input, length);

    return (char*)buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
    int inlen = strlen(input);

    char* inbuf = malloc(inlen+4);
    strcpy(inbuf, input);

    // Apple cut the padding off their challenges; restore it
    char pad = '=';
    while (inlen++ & 3)
        strncat(inbuf, &pad, 1);

    int bufsize = (inlen-1)*3/4;
    uint8_t *buf = malloc(bufsize);

    mbedtls_base64_decode(buf, bufsize, (size_t*)outlen, (unsigned char *)inbuf, strlen(inbuf));

    free(inbuf);
    return buf;
}

static const char super_secret_key[] =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
"wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
"wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
"/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
"UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
"BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
"LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
"NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
"lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
"aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
"a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
"oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
"oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
"k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
"AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
"cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
"54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
"17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
"1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
"LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
"2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
"-----END RSA PRIVATE KEY-----";

/**
 * Return a string representation of an mbedtls error code
 */
static char* mbedtlsError(int errnum) {
    static char buffer[200];
    mbedtls_strerror(errnum, buffer, sizeof(buffer));
    return buffer;
} // mbedtlsError

uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
    int ret = 0;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    if ( (ret = mbedtls_pk_parse_key(&pk, (unsigned char*)super_secret_key, strlen(super_secret_key)+1,
                                     NULL, 0)) != 0 ) {
        die("mbedtls_pk_parse_key error %s\n", mbedtlsError(ret)); //-0x%04x\n", -ret);
    }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    const char* pers="MyEntropy";

    mbedtls_ctr_drbg_seed(
            &ctr_drbg,
            mbedtls_entropy_func,
            &entropy,
            (const unsigned char*)pers,
            strlen(pers));

    uint8_t *out = NULL;
    mbedtls_rsa_context* ctx;
    switch (mode) {
        case RSA_MODE_AUTH:
            out = malloc(256);
            ret = mbedtls_rsa_pkcs1_encrypt(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &ctr_drbg,
                MBEDTLS_RSA_PRIVATE, inlen, input, out);
            *outlen = 256;
            break;
        case RSA_MODE_KEY:
            ctx = mbedtls_pk_rsa(pk);
            mbedtls_rsa_set_padding(ctx, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
            out = malloc(16);
            ret = mbedtls_rsa_pkcs1_decrypt(ctx, mbedtls_ctr_drbg_random, &ctr_drbg,
             MBEDTLS_RSA_PRIVATE, (size_t*)outlen, input, out, 16);
            break;
        default:
            die("bad rsa mode");
    }
    if (ret != 0)
        die("rsa_apply mode %d error: %s\n", mode, mbedtlsError(ret));

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_pk_free(&pk);
    return out;
}

void command_start(void) {
    if (!config.cmd_start)
        return;
    if (!config.cmd_blocking)
        return;

    debug(1, "running start command: %s", config.cmd_start);
    if (system(config.cmd_start))
        warn("exec of external start command failed");

    if (!config.cmd_blocking)
        exit(0);
}

void command_stop(void) {
    if (!config.cmd_stop)
        return;
    if (!config.cmd_blocking)
        return;

    debug(1, "running stop command: %s", config.cmd_stop);
    if (system(config.cmd_stop))
        warn("exec of external stop command failed");

    if (!config.cmd_blocking)
        exit(0);
}