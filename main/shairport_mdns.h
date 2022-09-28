#ifndef _MDNS_H
#define _MDNS_H

#define EXAMPLE_MDNS_INSTANCE CONFIG_MDNS_INSTANCE
#define CONFIG_MDNS_HOSTNAME    "ESP-WROVER"
#define CONFIG_MDNS_INSTANCE   "ESP with mDNS"

static const char * TAG = "mdns-test";

void mdns_unregister(void);
void mdns_register(void);

#define MDNS_RECORD  "tp=UDP", "sm=false", "ek=1", "et=0,1", "cn=0,1", "ch=2", \
                "ss=16", "sr=44100", "vn=3", "txtvers=1", "da=true", "md=0,1,2", \
                config.password ? "pw=true" : "pw=false"

#endif // _MDNS_H
