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
#include "mdns.h"

#define CONFIG_MDNS_HOSTNAME   "AirDAC"
#define CONFIG_MDNS_INSTANCE   "AirDAC mDNS"

void start_shairport(void* args);

char* hostname;

void app_main(void) {
    esp_log_level_set("RTSP", ESP_LOG_DEBUG);
    esp_log_level_set("RTP", ESP_LOG_DEBUG);
    esp_log_level_set("Player", ESP_LOG_DEBUG);
    hostname = strdup(CONFIG_MDNS_HOSTNAME);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    //initialize mDNS
    ESP_ERROR_CHECK( mdns_init() );
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK( mdns_hostname_set(hostname) );
    ESP_LOGI("TEST", "mdns hostname set to: [%s]", hostname);
    //set default mDNS instance name
    ESP_ERROR_CHECK( mdns_instance_name_set(CONFIG_MDNS_INSTANCE) );

    xTaskCreate(start_shairport, "Shairport", 8192, NULL, 1, NULL);
    return;
}

#define traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue) ESP_LOGI("TRACE", "Entering mutex %s", #pxQueue)