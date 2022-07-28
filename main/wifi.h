#ifndef __WIFI_H_
#define __WIFI_H_

typedef enum
{
    WIFI_OK = 0,
    WIFI_ERROR,
    TOTAL_WIFI_ERROR,
} wifi_err_t;

void       wifi_init(void);
void       wifi_scan(void);
wifi_err_t wifi_connect_sta(char* ssid, char* password);
void       wifi_disconnect(void);

#endif