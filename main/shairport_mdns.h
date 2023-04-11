#ifndef _MDNS_H
#define _MDNS_H

#define EXAMPLE_MDNS_INSTANCE CONFIG_MDNS_INSTANCE
#define CONFIG_MDNS_HOSTNAME    "ESP-WROVER"
#define CONFIG_MDNS_INSTANCE   "ESP with mDNS"

void mdns_unregister(void);
void mdns_register(void);

#endif // _MDNS_H
