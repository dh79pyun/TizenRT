/****************************************************************************
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License\n");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <wifi_manager/wifi_manager.h>

#define WM_TEST_COUNT  1

#define USAGE															\
	"\n usage: wm_test [options]\n"										\
	"\n run Wi-Fi Manager:\n"											\
	"	 wm_test start(default: station mode)\n"						\
	"	 wm_test stop\n"												\
	"	 wm_test stats\n"                                               \
	"\n softap mode options:\n"											\
	"	 wm_test softap [ssid] [password]\n"							\
	"\n station mode options:\n"										\
	"	 wm_test sta\n"													\
	"	 wm_test join [ssid] [security mode] [password]\n"				\
	"	    (1) [security mode] is optional if not open mode\n"			\
	"	    (2) [password] is unnecessary in case of open mode\n"		\
	"	 wm_test leave\n"												\
	"	 wm_test cancel\n"												\
	"\n run scan:\n"													\
	"	 wm_test scan\n"												\
	"\n get current state:\n"											\
	"	 wm_test mode\n\n"												\
	"\n set a profile:\n"											\
	"	 wm_test set [ssid] [security mode] [password]\n"			\
	"	 security mode examples : open, wep_shared \n"				\
	"	               wpa_aes, wpa_tkip, wpa_mixed  \n"			\
	"	               wpa2_aes, wpa2_tkip, wpa2_mixed  \n"			\
	"	               wpa12_aes, wpa12_tkip, wpa12_mixed  \n"		\
	"	               (*_ent for enterprise)  \n"					\
	"\n get a profile:\n"											\
	"	 wm_test get\n"												\
	"\n remove a profile:\n"										\
	"	 wm_test reset\n\n"											\
	"\n repeat test of APIs:\n"										\
	"	 wm_test auto [softap ssid] [softap password] [ssid] [security mode] [password]\n\n" \

typedef void (*test_func)(void *arg);

struct options {
	test_func func;
	uint16_t channel;
	char *ssid;
	char *bad_ssid;
	char *password;
	char *bad_password;
	wifi_manager_ap_auth_type_e    auth_type;
	wifi_manager_ap_crypto_type_e  crypto_type;
	char *softap_ssid;
	char *softap_password;
};

typedef int (*exec_func)(struct options *opt, int argc, char *argv[]);

/**
 * Internal functions
 */
static int wm_mac_addr_to_mac_str(char mac_addr[6], char mac_str[20]);
static int wm_mac_str_to_mac_addr(char mac_str[20], char mac_addr[6]);

/*
 * Signal
 */
void wm_signal_deinit(void);
int wm_signal_init(void);

/*
 * Callbacks
 */
static void wm_sta_connected(wifi_manager_result_e);
static void wm_sta_disconnected(wifi_manager_disconnect_e);
static void wm_softap_sta_join(void);
static void wm_softap_sta_leave(void);
static void wm_scan_done(wifi_manager_scan_info_s **scan_result, wifi_manager_scan_result_e res);

/*
 * Handler
 */
void wm_start(void *arg);
void wm_stop(void *arg);
void wm_softap_start(void *arg);
void wm_sta_start(void *arg);
void wm_connect(void *arg);
void wm_disconnect(void *arg);
void wm_cancel(void *arg); // stop reconnecting to wifi AP. it doesn't expect to receive a signal because AP is already disconnected
void wm_set_info(void *arg);
void wm_get_info(void *arg);
void wm_reset_info(void *arg);
void wm_scan(void *arg);
void wm_display_state(void *arg);
void wm_get_stats(void *arg);
void wm_auto_test(void *arg); // it tests softap mode and stations mode repeatedly

/*
 * Internal APIs
 */
static int _wm_test_softap(struct options *opt, int argc, char *argv[]);
static int _wm_test_join(struct options *opt, int argc, char *argv[]);
static int _wm_test_set(struct options *opt, int argc, char *argv[]);
static int _wm_test_auto(struct options *opt, int argc, char *argv[]);

static void wm_process(int argc, char *argv[]);
static int wm_parse_commands(struct options *opt, int argc, char *argv[]);

#ifdef CONFIG_EXAMPLES_WIFIMANAGER_STRESS_TOOL
extern void wm_run_stress_test(void *arg);
#endif

/*
 * Global
 */
static wifi_manager_cb_s wifi_callbacks = {
	wm_sta_connected,
	wm_sta_disconnected,
	wm_softap_sta_join,
	wm_softap_sta_leave,
	wm_scan_done,
};

static pthread_mutex_t g_wm_mutex = PTHREAD_MUTEX_INITIALIZER;;
static pthread_cond_t g_wm_cond;
static pthread_mutex_t g_wm_func_mutex = PTHREAD_MUTEX_INITIALIZER;;
static pthread_cond_t g_wm_func_cond;
static int g_mode = 0; // check program is running

#define WM_TEST_SIGNAL										\
	do {													\
		pthread_mutex_lock(&g_wm_mutex);					\
		printf("T%d send signal\n", getpid());	\
		pthread_cond_signal(&g_wm_cond);					\
		pthread_mutex_unlock(&g_wm_mutex);					\
	} while (0)

#define WM_TEST_WAIT								\
	do {											\
		pthread_mutex_lock(&g_wm_mutex);			\
		printf(" T%d wait signal\n", getpid());		\
		pthread_cond_wait(&g_wm_cond, &g_wm_mutex);	\
		pthread_mutex_unlock(&g_wm_mutex);			\
	} while (0)

#define WM_TEST_FUNC_SIGNAL								\
	do {												\
		pthread_mutex_lock(&g_wm_func_mutex);			\
		printf(" T%d send func signal\n", getpid());	\
		pthread_cond_signal(&g_wm_func_cond);			\
		pthread_mutex_unlock(&g_wm_func_mutex);			\
	} while (0)

#define WM_TEST_FUNC_WAIT										\
	do {														\
		pthread_mutex_lock(&g_wm_func_mutex);					\
		printf(" T%d wait func signal\n", getpid());			\
		pthread_cond_wait(&g_wm_func_cond, &g_wm_func_mutex);	\
		pthread_mutex_unlock(&g_wm_func_mutex);					\
	} while (0)

#define WM_TEST_LOG_START						\
	do {										\
		printf("-->%s\n", __FUNCTION__);		\
	} while (0)


#define WM_TEST_LOG_END							\
	do {										\
		printf("<--%s\n", __FUNCTION__);		\
	} while (0)

/*
 * Supported security method
 */
static const char *wifi_test_auth_method[] = {
	"open",
	"wep_shared",
	"wpa",
	"wpa2",
	"wpa12",
	"wpa",
	"wpa2",
	"wpa12",
	"ibss_open",
	"wps",
};

static const char *wifi_test_crypto_method[] = {
	"none",
	"64",
	"128",
	"aes",
	"tkip",
	"mixed",
	"aes_ent",
	"tkip_ent",
	"mixed_ent",
};

static const wifi_manager_ap_auth_type_e auth_type_table[] = {
	WIFI_MANAGER_AUTH_OPEN,                    /**<  open mode                                 */
	WIFI_MANAGER_AUTH_WEP_SHARED,              /**<  use shared key (wep key)                  */
	WIFI_MANAGER_AUTH_WPA_PSK,                 /**<  WPA_PSK mode                              */
	WIFI_MANAGER_AUTH_WPA2_PSK,                /**<  WPA2_PSK mode                             */
	WIFI_MANAGER_AUTH_WPA_AND_WPA2_PSK,        /**<  WPA_PSK and WPA_PSK mixed mode            */
	WIFI_MANAGER_AUTH_WPA_PSK_ENT,             /**<  Enterprise WPA_PSK mode                   */
	WIFI_MANAGER_AUTH_WPA2_PSK_ENT,            /**<  Enterprise WPA2_PSK mode                  */
	WIFI_MANAGER_AUTH_WPA_AND_WPA2_PSK_ENT,    /**<  Enterprise WPA_PSK and WPA_PSK mixed mode */
	WIFI_MANAGER_AUTH_IBSS_OPEN,               /**<  IBSS ad-hoc mode                          */
	WIFI_MANAGER_AUTH_WPS,                     /**<  WPS mode                                  */
	WIFI_MANAGER_AUTH_UNKNOWN,                 /**<  unknown type                              */
};

static const wifi_manager_ap_crypto_type_e crypto_type_table[] = {
	WIFI_MANAGER_CRYPTO_NONE,                  /**<  none encryption                           */
	WIFI_MANAGER_CRYPTO_WEP_64,                /**<  WEP encryption wep-40                     */
	WIFI_MANAGER_CRYPTO_WEP_128,               /**<  WEP encryption wep-104                    */
	WIFI_MANAGER_CRYPTO_AES,                   /**<  AES encryption                            */
	WIFI_MANAGER_CRYPTO_TKIP,                  /**<  TKIP encryption                           */
	WIFI_MANAGER_CRYPTO_TKIP_AND_AES,          /**<  TKIP and AES mixed encryption             */
	WIFI_MANAGER_CRYPTO_AES_ENT,               /**<  Enterprise AES encryption                 */
	WIFI_MANAGER_CRYPTO_TKIP_ENT,              /**<  Enterprise TKIP encryption                */
	WIFI_MANAGER_CRYPTO_TKIP_AND_AES_ENT,      /**<  Enterprise TKIP and AES mixed encryption  */
	WIFI_MANAGER_CRYPTO_UNKNOWN,               /**<  unknown encryption                        */
};

/*
 * Predefined functions
 */
typedef enum {
	WM_TEST_ERR = -1,
	WM_TEST_START,
	WM_TEST_STOP,
	WM_TEST_SOFTAP,
	WM_TEST_STA,
	WM_TEST_JOIN,
	WM_TEST_LEAVE,
	WM_TEST_CANCEL,
	WM_TEST_SET,
	WM_TEST_GET,
	WM_TEST_RESET,
	WM_TEST_SCAN,
	WM_TEST_MODE,
	WM_TEST_STATS,
	WM_TEST_AUTO,
	WM_TEST_STRESS,
	WM_TEST_MAX,
} wm_test_e;

test_func func_table[WM_TEST_MAX] = {
	wm_start,
	wm_stop,
	wm_softap_start,
	wm_sta_start,
	wm_connect,
	wm_disconnect,
	wm_cancel,
	wm_set_info,
	wm_get_info,
	wm_reset_info,
	wm_scan,
	wm_display_state,
	wm_get_stats,
	wm_auto_test,
#ifdef CONFIG_EXAMPLES_WIFIMANAGER_STRESS_TOOL
	wm_run_stress_test
#else
	NULL
#endif
};

exec_func exec_table[WM_TEST_MAX] = {
	NULL,                                      /* WM_TEST_START    */
	NULL,                                      /* WM_TEST_STOP     */
	_wm_test_softap,                           /* WM_TEST_SOFTAP   */
	NULL,                                      /* WM_TEST_STA      */
	_wm_test_join,                             /* WM_TEST_JOIN     */
	NULL,                                      /* WM_TEST_LEAVE    */
	NULL,                                      /* WM_TEST_CANCEL   */
	_wm_test_set,                              /* WM_TEST_SET      */
	NULL,                                      /* WM_TEST_GET      */
	NULL,                                      /* WM_TEST_RESET    */
	NULL,                                      /* WM_TEST_SCAN     */
	NULL,                                      /* WM_TEST_MODE     */
	NULL,                                      /* WM_TEST_STATS    */
	_wm_test_auto,                             /* WM_TEST_AUTO     */
	NULL                                       /* WM_TEST_STRESS   */
};

char *func_name[WM_TEST_MAX] = {
	"start",
	"stop",
	"softap",
	"sta",
	"join",
	"leave",
	"cancel",
	"set",
	"get",
	"reset",
	"scan",
	"mode",
	"stats",
	"auto",
	"stress"
};

static void print_wifi_ap_profile(wifi_manager_ap_config_s *config, char *title)
{
	printf("====================================\n");
	if (title) {
		printf("%s\n", title);
	}
	printf("------------------------------------\n");
	printf("SSID: %s\n", config->ssid);
	if (config->ap_auth_type == WIFI_MANAGER_AUTH_UNKNOWN || config->ap_crypto_type == WIFI_MANAGER_CRYPTO_UNKNOWN) {
		printf("SECURITY: unknown\n");
	} else {
		char security_type[20] = {0,};
		strcat(security_type, wifi_test_auth_method[config->ap_auth_type]);
		wifi_manager_ap_auth_type_e tmp_type = config->ap_auth_type;
		if (tmp_type == WIFI_MANAGER_AUTH_OPEN || tmp_type == WIFI_MANAGER_AUTH_IBSS_OPEN || tmp_type == WIFI_MANAGER_AUTH_WEP_SHARED) {
			printf("SECURITY: %s\n", security_type);
		} else {
			strcat(security_type, "_");
			strcat(security_type, wifi_test_crypto_method[config->ap_crypto_type]);
			printf("SECURITY: %s\n", security_type);
		}
	}
	printf("====================================\n");
}

static void print_wifi_softap_profile(wifi_manager_softap_config_s *config, char *title)
{
	printf("====================================\n");
	if (title) {
		printf("%s\n", title);
	}
	printf("------------------------------------\n");
	printf("SSID: %s\n", config->ssid);
	printf("channel: %d\n", config->channel);
	printf("====================================\n");
}

static wifi_manager_ap_auth_type_e get_auth_type(const char *method)
{
	char data[20];
	strcpy(data, method);

	char *result[3];
	char *next_ptr;
	result[0] = strtok_r(data, "_", &next_ptr);
	result[1] = strtok_r(NULL, "_", &next_ptr);
	result[2] = strtok_r(NULL, "_", &next_ptr);

	int i = 0;
	int list_size = sizeof(wifi_test_auth_method)/sizeof(wifi_test_auth_method[0]);
	for (; i < list_size; i++) {
		if ((strcmp(method, wifi_test_auth_method[i]) == 0) || (strcmp(result[0], wifi_test_auth_method[i]) == 0)) {
			if (result[2] != NULL) {
				if (strcmp(result[2], "ent") == 0) {
					return auth_type_table[i + 3];
				}
			}
			return auth_type_table[i];
		}
	}
	return WIFI_MANAGER_AUTH_UNKNOWN;
}

static wifi_manager_ap_crypto_type_e get_crypto_type(const char *method)
{
	char data[20];
	strcpy(data, method);

	char *result[2];
	char *next_ptr;
	result[0] = strtok_r(data, "_", &next_ptr);
	result[1] = next_ptr;

	int i = 0;
	int list_size = sizeof(wifi_test_crypto_method)/sizeof(wifi_test_crypto_method[0]);
	for (; i < list_size; i++) {
		if (strcmp(result[1], wifi_test_crypto_method[i]) == 0) {
			return crypto_type_table[i];
		}
	}
	return WIFI_MANAGER_CRYPTO_UNKNOWN;
}

/*
 * Internal functions
 */
static int wm_mac_addr_to_mac_str(char mac_addr[6], char mac_str[20])
{
	int wret = -1;
	if (mac_addr && mac_str) {
		snprintf(mac_str, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
		wret = OK;
	}
	return wret;
}

static int wm_mac_str_to_mac_addr(char mac_str[20], char mac_addr[6])
{
	int wret = -1;
	if (mac_addr && mac_str) {
		int ret = sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx%*c", &mac_addr[0], &mac_addr[1], &mac_addr[2], &mac_addr[3], &mac_addr[4], &mac_addr[5]);
		if (ret == WIFIMGR_MACADDR_LEN) {
			wret = OK;
		}
	}
	return wret;
}

/*
 * Signal
 */
int wm_signal_init(void)
{
	if (g_mode != 0) {
		printf("Program is already running\n");
		return -1;
	}
	g_mode = 1;
	int res = pthread_mutex_init(&g_wm_func_mutex, NULL);
	if (res != 0) {
		printf(" Pthread mutex func init fail(%d) (%d)\n", res, errno);
		return -1;
	}
	res = pthread_cond_init(&g_wm_func_cond, NULL);
	if (res != 0) {
		printf(" Conditional mutex func init fail(%d) (%d)\n", res, errno);
		return -1;
	}

	res = pthread_mutex_init(&g_wm_mutex, NULL);
	if (res != 0) {
		printf(" Pthread mutex init fail(%d) (%d)\n", res, errno);
		return -1;
	}

	res = pthread_cond_init(&g_wm_cond, NULL);
	if (res != 0) {
		printf(" Conditional mutex init fail(%d) (%d)\n", res, errno);
		return -1;
	}

	return 0;
}

void wm_signal_deinit(void)
{
	pthread_mutex_destroy(&g_wm_func_mutex);
	pthread_cond_destroy(&g_wm_func_cond);
	pthread_mutex_destroy(&g_wm_mutex);
	pthread_cond_destroy(&g_wm_cond);
	g_mode = 0;
}

/*
 * Callback
 */
void wm_sta_connected(wifi_manager_result_e res)
{
	printf(" T%d --> %s res(%d)\n", getpid(), __FUNCTION__, res);
	WM_TEST_SIGNAL;
}

void wm_sta_disconnected(wifi_manager_disconnect_e disconn)
{
	sleep(2);
	printf(" T%d --> %s\n", getpid(), __FUNCTION__);
	WM_TEST_SIGNAL;
}

void wm_softap_sta_join(void)
{
	printf(" T%d --> %s\n", getpid(), __FUNCTION__);
	WM_TEST_SIGNAL;
}

void wm_softap_sta_leave(void)
{
	printf(" T%d --> %s\n", getpid(), __FUNCTION__);
	WM_TEST_SIGNAL;
}

void wm_scan_done(wifi_manager_scan_info_s **scan_result, wifi_manager_scan_result_e res)
{
	printf(" T%d --> %s\n", getpid(), __FUNCTION__);
	/* Make sure you copy the scan results onto a local data structure.
	 * It will be deleted soon eventually as you exit this function.
	 */
	if (scan_result == NULL) {
		WM_TEST_SIGNAL;
		return;
	}
	wifi_manager_scan_info_s *wifi_scan_iter = *scan_result;
	while (wifi_scan_iter != NULL) {
		printf("WiFi AP SSID: %-25s, BSSID: %-20s, Rssi: %d, Auth: %d, Crypto: %d\n",
			   wifi_scan_iter->ssid, wifi_scan_iter->bssid, wifi_scan_iter->rssi,
			   wifi_scan_iter->ap_auth_type, wifi_scan_iter->ap_crypto_type);
		wifi_scan_iter = wifi_scan_iter->next;
	}
	WM_TEST_SIGNAL;
}

/*
 * Control Functions
 */
void wm_start(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;

	res = wifi_manager_init(&wifi_callbacks);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf(" wifi_manager_init fail\n");
	}
	WM_TEST_LOG_END;
}

void wm_stop(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = wifi_manager_deinit();
	if (res != WIFI_MANAGER_SUCCESS) {
		printf(" WiFi Manager failed to stop\n");
	}
	WM_TEST_LOG_END;
}

void wm_softap_start(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;
	struct options *ap_info = (struct options *)arg;
	if (strlen(ap_info->ssid) > WIFIMGR_SSID_LEN || strlen(ap_info->password) > WIFIMGR_PASSPHRASE_LEN) {
		printf("Param Error\n");
		WM_TEST_LOG_END;
		return;
	}
	wifi_manager_softap_config_s ap_config;
	strncpy(ap_config.ssid, ap_info->ssid, WIFIMGR_SSID_LEN-1);
	ap_config.ssid[WIFIMGR_SSID_LEN-1] = '\0';
	strncpy(ap_config.passphrase, ap_info->password, WIFIMGR_PASSPHRASE_LEN-1);
	ap_config.passphrase[WIFIMGR_PASSPHRASE_LEN-1] = '\0';
	ap_config.channel = 1;

	print_wifi_softap_profile(&ap_config, "AP INFO");

	res = wifi_manager_set_mode(SOFTAP_MODE, &ap_config);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf(" Run SoftAP Fail\n");
	}
	WM_TEST_LOG_END;
}

void wm_sta_start(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;

	res = wifi_manager_set_mode(STA_MODE, NULL);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf(" Set STA mode Fail\n");
		return;
	}
	printf("Start STA mode\n");
	WM_TEST_LOG_END;
}

void wm_connect(void *arg)
{
	WM_TEST_LOG_START;
	struct options *ap_info = (struct options *)arg;
	wifi_manager_ap_config_s apconfig;
	strncpy(apconfig.ssid, ap_info->ssid, WIFIMGR_SSID_LEN);
	apconfig.ssid_length = strlen(ap_info->ssid);
	apconfig.ssid[WIFIMGR_SSID_LEN] = '\0';
	apconfig.ap_auth_type = ap_info->auth_type;
	if (ap_info->auth_type != WIFI_MANAGER_AUTH_OPEN) {
		strncpy(apconfig.passphrase, ap_info->password, WIFIMGR_PASSPHRASE_LEN);
		apconfig.passphrase[WIFIMGR_PASSPHRASE_LEN] = '\0';
		apconfig.passphrase_length = strlen(ap_info->password);
		apconfig.ap_crypto_type = ap_info->crypto_type;
	} else{
		apconfig.passphrase[0] = '\0';
		apconfig.passphrase_length = 0;
		apconfig.ap_crypto_type = ap_info->crypto_type;
	}

	print_wifi_ap_profile(&apconfig, "Connecting AP Info");

	wifi_manager_result_e res = wifi_manager_connect_ap(&apconfig);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf(" AP connect failed\n");
		return;
	}
	/* Wait for DHCP connection */
	printf(" wait join done\n");
	WM_TEST_WAIT;

	WM_TEST_LOG_END;
}

void wm_disconnect(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;
	/* Disconnect AP */
	res = wifi_manager_disconnect_ap();
	if (res != WIFI_MANAGER_SUCCESS) {
		printf("disconnect fail (%d)\n", res);
		return;
	}
	WM_TEST_WAIT;
	WM_TEST_LOG_END;;
}

void wm_cancel(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;
	/* Disconnect AP */
	res = wifi_manager_disconnect_ap();
	if (res != WIFI_MANAGER_SUCCESS) {
		printf("disconnect fail (%d)\n", res);
		return;
	}
	WM_TEST_LOG_END;
}

void wm_set_info(void *arg)
{
	WM_TEST_LOG_START;
	struct options *ap_info = (struct options *)arg;
	wifi_manager_ap_config_s apconfig;
	strncpy(apconfig.ssid, ap_info->ssid, WIFIMGR_SSID_LEN);
	apconfig.ssid_length = strlen(ap_info->ssid);
	apconfig.ssid[WIFIMGR_SSID_LEN] = '\0';
	apconfig.ap_auth_type = ap_info->auth_type;
	if (apconfig.ap_auth_type != WIFI_MANAGER_AUTH_OPEN) {
		strncpy(apconfig.passphrase, ap_info->password, WIFIMGR_PASSPHRASE_LEN);
		apconfig.passphrase[WIFIMGR_PASSPHRASE_LEN] = '\0';
		apconfig.passphrase_length = strlen(ap_info->password);

	} else {
		strcpy(apconfig.passphrase, "");
		apconfig.passphrase_length = 0;
	}

	apconfig.ap_crypto_type = ap_info->crypto_type;

	print_wifi_ap_profile(&apconfig, "Set AP Info");

	wifi_manager_result_e res = wifi_manager_save_config(&apconfig);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf("Save AP configuration failed\n");
		return;
	}
	WM_TEST_LOG_END;
}

void wm_get_info(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_ap_config_s apconfig;
	wifi_manager_result_e res = wifi_manager_get_config(&apconfig);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf("Get AP configuration failed\n");
		return;
	}
	print_wifi_ap_profile(&apconfig, "Stored Wi-Fi Infomation");

	WM_TEST_LOG_END;
}

void wm_reset_info(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = wifi_manager_remove_config();
	if (res != WIFI_MANAGER_SUCCESS) {
		printf("Get AP configuration failed\n");
		return;
	}

	WM_TEST_LOG_END;
}

void wm_scan(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;

	res = wifi_manager_scan_ap();
	if (res != WIFI_MANAGER_SUCCESS) {
		printf(" scan Fail\n");
		return;
	}
	WM_TEST_WAIT; // wait the scan result
	WM_TEST_LOG_END;
}

void wm_display_state(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_info_s info;
	char mac_str[20];
	char mac_char[6];
	wifi_manager_result_e res = wifi_manager_get_info(&info);
	if (res != WIFI_MANAGER_SUCCESS) {
		goto exit;
	}
	if (info.mode == SOFTAP_MODE) {
		if (info.status == CLIENT_CONNECTED) {
			printf("MODE: softap (client connected)\n");
		} else if (info.status == CLIENT_DISCONNECTED) {
			printf("MODE: softap (no client)\n");
		}
		printf("IP: %s\n", info.ip4_address);
		printf("SSID: %s\n", info.ssid);
		if (wm_mac_addr_to_mac_str(info.mac_address, mac_str) < 0) {
			goto exit;
		}
		printf("MAC: %s\n", mac_str);
		if (wm_mac_str_to_mac_addr(mac_str, mac_char) < 0) {
			goto exit;
		}
	} else if (info.mode == STA_MODE) {
		if (info.status == AP_CONNECTED) {
			printf("MODE: station (connected)\n");
			printf("IP: %s\n", info.ip4_address);
			printf("SSID: %s\n", info.ssid);
			printf("rssi: %d\n", info.rssi);
		} else if (info.status == AP_DISCONNECTED) {
			printf("MODE: station (disconnected)\n");
		} else if (info.status == AP_RECONNECTING) {
			printf("MODE: station (reconnecting)\n");
			printf("IP: %s\n", info.ip4_address);
			printf("SSID: %s\n", info.ssid);
		}
		if (wm_mac_addr_to_mac_str(info.mac_address, mac_str) < 0) {
			goto exit;
		}
		printf("MAC: %s\n", mac_str);
		if (wm_mac_str_to_mac_addr(mac_str, mac_char) < 0) {
			goto exit;
		}
	} else {
		printf("STATE: NONE\n");
	}
exit:
	WM_TEST_LOG_END;
	return;
}

void wm_get_stats(void *arg)
{
	WM_TEST_LOG_START;
	wifi_manager_stats_s stats;
	wifi_manager_result_e res = wifi_manager_get_stats(&stats);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf("Get WiFi Manager stats failed\n");
	} else {
		printf("=======================================================================\n");
		printf("CONN    CONNFAIL    DISCONN    RECONN    SCAN    SOFTAP    JOIN    LEFT\n");
		printf("%-8d%-12d%-11d%-10d", stats.connect, stats.connectfail, stats.disconnect, stats.reconnect);
		printf("%-8d%-10d%-8d%-8d\n", stats.scan, stats.softap, stats.joined, stats.left);
		printf("=======================================================================\n");
	}
	WM_TEST_LOG_END;
}

void wm_auto_test(void *arg)
{
	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;
	struct options *info = (struct options *)arg;
	/* Set SoftAP Configuration */
	wifi_manager_softap_config_s softap_config;
	strncpy(softap_config.ssid, info->softap_ssid, WIFIMGR_SSID_LEN);
	softap_config.ssid[WIFIMGR_SSID_LEN] = '\0';
	strncpy(softap_config.passphrase, info->softap_password, WIFIMGR_PASSPHRASE_LEN);
	softap_config.passphrase[WIFIMGR_PASSPHRASE_LEN] = '\0';
	softap_config.channel = 1;

	/* Set AP Configuration */
	wifi_manager_ap_config_s ap_config;
	strncpy(ap_config.ssid, info->ssid, WIFIMGR_SSID_LEN);
	ap_config.ssid_length = strlen(info->ssid);
	ap_config.ssid[WIFIMGR_SSID_LEN] = '\0';
	strncpy(ap_config.passphrase, info->password, WIFIMGR_PASSPHRASE_LEN);
	ap_config.passphrase_length = strlen(info->password);
	ap_config.passphrase[WIFIMGR_PASSPHRASE_LEN] = '\0';
	ap_config.ap_auth_type = info->auth_type;
	ap_config.ap_crypto_type = info->crypto_type;

	printf("Init WiFi (default STA mode)\n");
	res = wifi_manager_init(NULL);
	res = wifi_manager_init(&wifi_callbacks);
	if (res != WIFI_MANAGER_SUCCESS) {
		printf("wifi_manager_init fail\n");
		return;
	}
	/* Print current status */
	wm_display_state(NULL);

	printf("====================================\n");
	printf("Repeated Test\n");
	printf("Total: %d\n", WM_TEST_COUNT);
	printf("====================================\n");
	print_wifi_ap_profile(&ap_config, "");
	print_wifi_softap_profile(&softap_config, "SoftAP Info");

	int cnt = 0;
	while (cnt++ < WM_TEST_COUNT) {
		printf(" T%d Starting round %d\n", getpid(), cnt);
		/* Print current status */
		wm_display_state(NULL);

		/* Connect to AP */
		printf("Connecting to AP\n");
		print_wifi_ap_profile(&ap_config, "Connecting AP Info");
		res = wifi_manager_connect_ap(&ap_config);
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("AP connect failed in round %d\n", cnt);
			return;
		} else {
			WM_TEST_WAIT;
		}

		/* Print current status */
		wm_display_state(NULL); //check dhcp

		/* Start SoftAP mode */
		printf("Start SoftAP mode\n");
		res = wifi_manager_set_mode(SOFTAP_MODE, &softap_config);
		if (res != WIFI_MANAGER_SUCCESS) {
			printf(" Set AP mode Fail\n");
			return;
		}
		/* Print current status */
		wm_display_state(NULL);

		/* Scanning */
		printf("Start scanning\n");
		res = wifi_manager_scan_ap();
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("scan Fail\n");
			return;
		} else {
			WM_TEST_WAIT; // wait the scan result
		}

		/* Print current status */
		wm_display_state(NULL);

		/* Start STA mode */
		printf("Start STA mode\n");
		res = wifi_manager_set_mode(STA_MODE, NULL);
		if (res != WIFI_MANAGER_SUCCESS) {
			printf(" Set STA mode Fail\n");
			return;
		}

		/* Scanning */
		printf("Start scanning\n");
		res = wifi_manager_scan_ap();
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("scan Fail\n");
			return;
		} else {
			WM_TEST_WAIT; // wait the scan result
		}

		/* Connect to AP */
		printf("Connecting to AP\n");
		res = wifi_manager_connect_ap(&ap_config);
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("AP connect failed in round %d\n", cnt);
			return;
		} else {
			WM_TEST_WAIT; // wait dhcp
		}

		/* File system call */
		printf("Save AP info.\n");
		res = wifi_manager_save_config(&ap_config);
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("Save AP configuration failed\n");
			return;
		}

		printf("Get AP info.\n");
		wifi_manager_ap_config_s new_config;
		res = wifi_manager_get_config(&new_config);
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("Get AP configuration failed\n");
			return;
		}

		print_wifi_ap_profile(&new_config, "Stored WiFi Information");
		printf("Reset AP info.\n");
		res = wifi_manager_remove_config();
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("Reset AP configuration failed\n");
			return;
		}

		/* Print current status */
		wm_display_state(NULL);

		/* Disconnect AP */
		printf("Disconnecting AP\n");
		res = wifi_manager_disconnect_ap();
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("disconnect fail (%d)\n", res);
			return;
		} else {
			WM_TEST_WAIT;
		}

		/* Print current status */
		wm_display_state(NULL);

		printf("Deinit TEST in disconnected state\n");
		res = wifi_manager_deinit();
		if (res != WIFI_MANAGER_SUCCESS) {
			printf("WiFi Manager failed to stop\n");
			return;
		}

		printf("Cycle finished [Round %d]\n", cnt);
	}
	printf("Exit WiFi Manager Stress Test..\n");

	return;
}

static wm_test_e _wm_get_opt(int argc, char *argv[])
{
	int idx = 0;
	if (argc < 3) {
		return WM_TEST_ERR;
	}

	for (idx = 0; idx < WM_TEST_MAX; idx++) {
		if (strcmp(argv[2], func_name[idx]) == 0) {
			return (wm_test_e)idx;
		}
	}

	return WM_TEST_ERR;
}

static int _wm_test_softap(struct options *opt, int argc, char *argv[])
{
	/* wpa2 aes is a default security mode. */
	opt->ssid = argv[3];
	opt->password = argv[4];
	return 0;
}

static int _wm_test_join(struct options *opt, int argc, char *argv[])
{
	if (argc < 5) {
		return -1;
	}
	opt->ssid = argv[3];
	opt->auth_type = get_auth_type(argv[4]);
	if (opt->auth_type == WIFI_MANAGER_AUTH_OPEN || opt->auth_type == WIFI_MANAGER_AUTH_IBSS_OPEN) {
		// case: open mode
		opt->password = "";
		opt->crypto_type = WIFI_MANAGER_CRYPTO_NONE;
		return 0;
	}

	if (argc == 5) {
		// case: unspecified security mode
		opt->auth_type = WIFI_MANAGER_AUTH_UNKNOWN;
		opt->crypto_type = WIFI_MANAGER_CRYPTO_UNKNOWN;
		opt->password = argv[4];
		return 0;
	}

	// case: security mode + password
	if (opt->auth_type == WIFI_MANAGER_AUTH_UNKNOWN) {
		return -1;
	}

	if (opt->auth_type == WIFI_MANAGER_AUTH_WEP_SHARED) {
		if (strlen(argv[5]) == 13) {
			opt->crypto_type = WIFI_MANAGER_CRYPTO_WEP_128;
		} else if (strlen(argv[5]) == 5) {
			opt->crypto_type = WIFI_MANAGER_CRYPTO_WEP_64;
		} else {
			return -1;
		}
	} else {
		opt->crypto_type = get_crypto_type(argv[4]);
		if (opt->crypto_type == WIFI_MANAGER_CRYPTO_UNKNOWN) {
			return -1;
		}
	}
	opt->password = argv[5];
	return 0;
}

static int _wm_test_set(struct options *opt, int argc, char *argv[])
{
	if (argc < 5) {
		return -1;
	}
	opt->ssid = argv[3];
	opt->auth_type = get_auth_type(argv[4]);
	if (opt->auth_type == WIFI_MANAGER_AUTH_UNKNOWN) {
		return -1;
	}
	if (opt->auth_type == WIFI_MANAGER_AUTH_OPEN || opt->auth_type == WIFI_MANAGER_AUTH_IBSS_OPEN) {
		opt->crypto_type = WIFI_MANAGER_CRYPTO_NONE;
		opt->password = "";
		return 0;
	}

	if (opt->auth_type == WIFI_MANAGER_AUTH_WEP_SHARED) {
		if (strlen(argv[5]) == 13) {
			opt->crypto_type = WIFI_MANAGER_CRYPTO_WEP_128;
		} else if (strlen(argv[5]) == 5) {
			opt->crypto_type = WIFI_MANAGER_CRYPTO_WEP_64;
		} else {
			return -1;
		}
	} else {
		opt->crypto_type = get_crypto_type(argv[4]);
		if (opt->crypto_type == WIFI_MANAGER_CRYPTO_UNKNOWN) {
			return -1;
		}
	}
	opt->password = argv[5];
	return 0;
}

static int _wm_test_auto(struct options *opt, int argc, char *argv[])
{
	if (argc < 7) {
		return -1;
	}
	opt->softap_ssid = argv[3];
	opt->softap_password = argv[4];
	opt->ssid = argv[5];
	opt->auth_type = get_auth_type(argv[6]);
	if (opt->auth_type == WIFI_MANAGER_AUTH_UNKNOWN) {
		return -1;
	}
	if (opt->auth_type == WIFI_MANAGER_AUTH_OPEN) {
		return 0;
	}
	opt->crypto_type = get_crypto_type(argv[6]);
	opt->password = argv[7];
	return 0;
}

static int wm_parse_commands(struct options *opt, int argc, char *argv[])
{
	int ret = 0;
	wm_test_e options = _wm_get_opt(argc, argv);
	if (options == WM_TEST_ERR) {
		return -1;
	}

	opt->func = func_table[options];
	if (opt->func == NULL) {
		return -1;
	}

	if (exec_table[options]) {
		ret = (exec_table[options])(opt, argc, argv);
	}

	return ret;
}

static void wm_process(int argc, char *argv[])
{
	struct options opt;
	int res = wm_parse_commands(&opt, argc, argv);
	if (res < 0) {
		printf("%s", USAGE);
		goto exit;
	}
	opt.func((void *)&opt);
exit:
	WM_TEST_FUNC_SIGNAL;
}

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int wm_test_main(int argc, char *argv[])
#endif
{
	printf("wifi manager test!!\n");
	int res = wm_signal_init();
	if (res < 0) {
		return -1;
	}
	task_create("wifi test sample", 100, 1024 * 10, (main_t)wm_process, argv);

	WM_TEST_FUNC_WAIT;

	wm_signal_deinit();

	return 0;
}
