#include <include/datalogger_control.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_modem.h"
#include "esp_modem_netif.h"
#include "esp_log.h"
#include "sarag450.h"
#include "datalogger_driver.h"

extern bool gsm_time_status;

static const char *TAG = "gprs_network_control";
static esp_netif_t *esp_netif = NULL;
static modem_dce_t *dce = NULL;
static modem_dte_t *dte = NULL;

static void *modem_netif_adapter;

static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int STOP_BIT = BIT1;
static bool network_ready = false;

static void init_netif(void);
static void init_modem(void);
static void on_ip_event(void *arg, esp_event_base_t event_base,
                    int32_t event_id, void *event_data);
static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                    int32_t event_id, void *event_data);
static void modem_event_handler(void *event_handler_arg, esp_event_base_t event_base, 
                    int32_t event_id, void *event_data);

void gprs_network_setup(void)
{
    network_ready = false;
    event_group = xEventGroupCreate();
    esp_modem_set_apn(get_apn());

    init_modem();
    init_netif();
}

bool gprs_network_connect(void)
{
    bool connected = false;
    esp_netif_auth_type_t auth_type = NETIF_PPP_AUTHTYPE_PAP;
    if(dce != NULL) {
        ESP_ERROR_CHECK(dce->set_flow_ctrl(dce, MODEM_FLOW_CONTROL_NONE));
        ESP_ERROR_CHECK(dce->store_profile(dce));
        /* Print Module ID, Operator, IMEI, IMSI */
        ESP_LOGI(TAG, "Module: %s", dce->name);
        ESP_LOGI(TAG, "Operator: %s", dce->oper);
        ESP_LOGI(TAG, "IMEI: %s", dce->imei);
        ESP_LOGI(TAG, "IMSI: %s", dce->imsi);
        /* Get signal quality */
        uint32_t rssi = 0, ber = 0;
        ESP_ERROR_CHECK(dce->get_signal_quality(dce, &rssi, &ber));
        ESP_LOGI(TAG, "rssi: %d, ber: %d", rssi, ber);
        set_csq(rssi);

        set_csq(rssi);

/*        if(rssi == 0)
        {
            blink_profile_set(LED_SIGNAL, BLINK_PROFILE_NO_SIGNAL);
        } else if (rssi <= 6)
        {
            blink_profile_set(LED_SIGNAL, BLINK_PROFILE_WEAK_SIGNAL);
        } else if (rssi <= 15)
        {
            blink_profile_set(LED_SIGNAL, BLINK_PROFILE_FAIR_SIGNAL);
        } else if (rssi <= 32)
        {
            blink_profile_set(LED_SIGNAL, BLINK_PROFILE_STRONG_SIGNAL);
        }*/

        /* Get battery voltage */

/*         uint32_t voltage = 0;
         voltage=get_battery_voltage();
         ESP_LOGI(TAG, "Battery voltage: %d mV", voltage);
         set_battery(voltage);*/

        /* setup PPPoS network parameters */
        ESP_LOGI(TAG, "username: %s pwd: %s", get_gprs_user(), get_gprs_pw());
        esp_netif_ppp_set_auth(esp_netif, auth_type, get_gprs_user(), get_gprs_pw());

        modem_netif_adapter = esp_modem_netif_setup(dte);
        esp_modem_netif_set_default_handlers(modem_netif_adapter, esp_netif);
        /* attach the modem to the network interface */
        esp_netif_attach(esp_netif, modem_netif_adapter);
        /* Wait for IP address */
        EventBits_t uxBits = xEventGroupWaitBits(event_group, CONNECT_BIT, pdTRUE, pdTRUE, 15000 / portTICK_PERIOD_MS);

        if ((uxBits & CONNECT_BIT) == 0)
        {
            network_ready = false;
            ESP_LOGI(TAG, "Connection timed out");
            esp_netif_deinit();
        }
        else
        {
            connected = true;
            if (gsm_time_status)
            {
             save_system_config_data_time();
            }

        }
    }
    return connected;
}

bool is_gprs_network_ready(void)
{
    return network_ready;
}

static void init_netif(void)
{
    esp_event_loop_delete_default();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
    if(esp_netif == NULL)
    {
        esp_netif = esp_netif_new(&cfg);
        assert(esp_netif);
    }
    
}

static void init_modem(void)
{
    turn_on_modem();

    esp_modem_dte_config_t config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    config_modem_uart(&config);

    if(dte == NULL) {
        dte = esp_modem_dte_init(&config);

        /* Register event handler */
        ESP_ERROR_CHECK(esp_modem_set_event_handler(dte, modem_event_handler, ESP_EVENT_ANY_ID, NULL));
    }

/*#if CONFIG_MODEM_DEVICE_SIM800
    dce = sim800_init(dte);*/
//#elif CONFIG_MODEM_DEVICE_SARA_G450
  //  dce = sarag450_init(dte);
/*#else
#error "Unsupported DCE"
#endif*/
}

void power_down_gprs(void)
{
	    if(dce != NULL) {
	 	   ESP_ERROR_CHECK(esp_modem_stop_ppp(dte));
	 	  /* Wait for the PPP connection to terminate gracefully */
	 	  xEventGroupWaitBits(event_group, STOP_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
	 	   turn_off_modem();
	 	    /* Unregister events, destroy the netif adapter and destroy its esp-netif instance */
	 	  ESP_ERROR_CHECK(dce->deinit(dce));

/*	 	   esp_modem_netif_clear_default_handlers(modem_netif_adapter);
	 	   esp_modem_netif_teardown(modem_netif_adapter);
//	 	   esp_netif_destroy(esp_netif);


	 	    vEventGroupDelete(event_group);
	 	    event_group = NULL;
	 	    ESP_LOGI(TAG, ">>>All deleted<<<");*/
	    }

}

//CALLBACKS
static void modem_event_handler(void *event_handler_arg, esp_event_base_t event_base, 
                                int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ESP_MODEM_EVENT_PPP_START:
        ESP_LOGI(TAG, "Modem PPP Started");
        break;
    case ESP_MODEM_EVENT_PPP_STOP:
        ESP_LOGI(TAG, "Modem PPP Stopped");
        xEventGroupSetBits(event_group, STOP_BIT);
        break;
    case ESP_MODEM_EVENT_UNKNOWN:
        ESP_LOGW(TAG, "Unknow line received: %s", (char *)event_data);
        break;
    default:
        break;
    }
}

static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %d", event_id);
    if (event_id == NETIF_PPP_ERRORUSER) {
        /* User interrupted event from esp-netif */
        esp_netif_t *netif = event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", netif);
    }
}


static void on_ip_event(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %d", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");
        network_ready = true;
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
        network_ready = false;
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}
