//
// Created by Luke on 2022/09/29.
//

#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "protocol_examples_common.h"
#include "netdb.h"

#include "mbedtls/aes.h"
void start_shairport(void* args);

void app_main(void) {

//    unsigned char* packet = malloc(2048);
//    static mbedtls_aes_context aes;
//    const uint8_t aes_key[] = {0x76, 0xbb, 0xf0, 0xc6, 0xc8, 0xdc, 0x62, 0xb2, 0xdf, 0xf9, 0x5d, 0x83, 0x89, 0xea, 0xba, 0xb5};
//    mbedtls_aes_init(&aes);
//    mbedtls_aes_setkey_dec(&aes, aes_key, 128);
//    const uint8_t iv[] = {0xde, 0x1c, 0xd8, 0x2a, 0x8f, 0xf9, 0x9d, 0x43, 0xeb, 0xc3, 0x1e, 0x16, 0x4c, 0x41, 0x3b, 0x23};
//    const uint8_t input[] = {0x44, 0xe0, 0xb2, 0x62, 0xa8, 0xf6, 0x81, 0x38, 0x7c, 0x30, 0x5c, 0x74, 0x90, 0x46, 0xe1, 0x84, 0xde, 0x1c, 0xd8, 0x2a, 0x8f, 0xf9, 0x9d, 0x43, 0xeb, 0xc3, 0x1e, 0x16, 0x4c, 0x41, 0x3b, 0x23};
//    size_t len = sizeof(input);
//    int aeslen = sizeof(input) & ~0xf;
//    int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, aeslen, iv, input, packet);
//    memcpy(packet+aeslen, input+aeslen, len-aeslen);

    //printf("\nresult: %d", ret);
    //input 44 e0 b2 62 a8 f6 81 38 7c 30 5c 74 90 46 e1 84 de 1c d8 2a 8f f9 9d 43 eb c3 1e 16 4c 41 3b 23
    //IV de 1c d8 2a 8f f9 9d 43 eb c3 1e 16 4c 41 3b 23
    //Key 76 bb f0 c6 c8 dc 62 b2 df f9 5d 83 89 ea ba b5

//    printf("Decrypted: \n");
//    for (int i = 0; i < len; i++) {
//        printf("0x%02x, ", packet[i]);
//    }


    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    xTaskCreate(start_shairport, "Shairport", 8192, NULL, 1, NULL);
    return;
}

#define traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue) ESP_LOGI("TRACE", "Entering mutex %s", #pxQueue)