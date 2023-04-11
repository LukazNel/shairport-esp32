/*
 * mDNS registration handler. This file is part of Shairport.
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

#include <string.h>
#include "common.h"
#include "shairport_mdns.h"

#include "esp_log.h"
#include "mdns.h"

void mdns_register(void) {
    char *mdns_apname = malloc(strlen(hostname) + 14);
    char *p = mdns_apname;
    int i;
    for (i=0; i<6; i++) {
        sprintf(p, "%02X", config.hw_addr[i]);
        p += 2;
    }
    *p++ = '@';
    strcpy(p, hostname);

    mdns_txt_item_t txt[] = {
            {"tp", "UDP"},
            {"sm", "false"},
            {"ek", "1"},
            {"et", "0,1"},
            {"cn", "0,1"},
            {"ch", "2"},
            {"ss", "16"},
            {"sr", "44100"},
            {"vn", "3"}, 
            {"txtvers", "1"},
            {"da", "true"},
            {"md", "0"},
            {"pw", "false"}
    };

    ESP_ERROR_CHECK( mdns_service_add(mdns_apname, "_raop", "_tcp", config.port, txt, 12) );
}

void mdns_unregister(void) {
    ESP_ERROR_CHECK( mdns_service_remove("_raop", "_tcp") );
}

