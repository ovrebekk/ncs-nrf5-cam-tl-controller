/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/printk.h>
#include <sys/byteorder.h>
#include <zephyr.h>
#include <drivers/gpio.h>
#include <soc.h>
#include <time.h>
#include <stdio.h>
/* clock_gettime() prototype */
#include <posix/time.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <bluetooth/services/nus.h>

#include <settings/settings.h>

#include <dk_buttons_and_leds.h>

#include "cam_tl_control.h"

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)


#define RUN_STATUS_LED          DK_LED1
#define CON_STATUS_LED          DK_LED2
#define RUN_LED_BLINK_INTERVAL  100

#define USER_LED                DK_LED3

#define USER_BUTTON             DK_BTN1_MSK

#define UART_BUF_SIZE CONFIG_BT_NUS_UART_BUFFER_SIZE

// Until the kit is configured through the app, run at a constant interval (since accurate time is not set)
static bool m_time_set_from_app = false;
static int m_picture_interval_s = 10*60;
static int m_pic_cap_start_hour = 8;
static int m_pic_cap_start_min = 0;
static int m_pic_cap_end_hour = 17;
static int m_pic_cap_end_min = 59;
static bool m_nus_notifications_enabled = false;

static struct bt_gatt_exchange_params exchange_params;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

struct bt_conn *m_conn = 0;

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
			  struct bt_gatt_exchange_params *params)
{
	struct bt_conn_info info = {0};
	int err;

	printk("MTU exchange %s\n", att_err == 0 ? "successful" : "failed");

	err = bt_conn_get_info(conn, &info);
	if (err) {
		printk("Failed to get connection info %d\n", err);
		return;
	}
}


static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
		return;
	}

	printk("Connected\n");

	dk_set_led_on(CON_STATUS_LED);

	exchange_params.func = exchange_func;

	err = bt_gatt_exchange_mtu(conn, &exchange_params);
	if (err) {
		printk("MTU exchange failed (err %d)\n", err);
	} else {
		printk("MTU exchange pending\n");
	}
	
	m_conn = conn;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);

	dk_set_led_off(CON_STATUS_LED);

	m_nus_notifications_enabled = false;
}

#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d\n", addr, level,
			err);
	}
}
#endif

static struct bt_conn_cb conn_callbacks = {
	.connected        = connected,
	.disconnected     = disconnected,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_LBS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
#endif

static volatile bool take_picture_override = false;
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON) {
		uint32_t user_button_state = button_state & USER_BUTTON;

		if(user_button_state) take_picture_override = true;
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}
static int convert_ascii_int(const uint8_t *str_ptr, int num_digits)
{
	int ret_val = 0;
	int multiplier = 1;
	for(int i = 0; i < (num_digits - 1); i++) multiplier *= 10;
	for(int i = 0; i < num_digits; i++) {
		if(str_ptr[i] >= '0' && str_ptr[i] <= '9'){
			ret_val += ((str_ptr[i] - '0') * multiplier);
			multiplier /= 10;
		}
		else return 0;
	}
	return ret_val;
}

static int send_nus_response_str(char *response)
{
	if(m_nus_notifications_enabled && strlen(response) > 0) {
		bt_nus_send(m_conn, response, strlen(response));
		return 0;
	}
	return -1;
}

static volatile bool time_update_requested = false;
static volatile bool take_picture_requested = false;

void on_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	struct tm set_time;
	static char response_msg[128];
	memcpy(response_msg, data, len);
	response_msg[len+1] = 0;
	//printk("NUS CMD received (len %i): %s\n", len, response_msg);
	if(len >= 2){
		// Set time command
		if(data[0] == 's' && data[1] == 't' && len == 14){
			set_time.tm_year = convert_ascii_int(data + 2, 2) + 100;
			set_time.tm_mon = convert_ascii_int(data + 4, 2);
			set_time.tm_mday = convert_ascii_int(data + 6, 2);
			set_time.tm_hour = convert_ascii_int(data + 8, 2);
			set_time.tm_min = convert_ascii_int(data + 10, 2);
			set_time.tm_sec = convert_ascii_int(data + 12, 2);
			time_t t = mktime(&set_time);
			struct timespec ts;
			ts.tv_sec = t;
			ts.tv_nsec = 0;
			clock_settime(CLOCK_REALTIME, &ts);
			m_time_set_from_app = true;
			sprintf(response_msg, "Time set over NUS: %s", asctime(&set_time));
		}
		// Set capture interval command
		else if(data[0] == 's' && data[1] == 'i' && len == 6){
			m_picture_interval_s = convert_ascii_int(data + 2, 4);
			if(m_picture_interval_s < 30) m_picture_interval_s = 30;
			sprintf(response_msg, "Picture interval set to %i", m_picture_interval_s);
		}
		// Set capture start time command
		else if(data[0] == 'c' && data[1] == 's' && len == 6){
			m_pic_cap_start_hour = convert_ascii_int(data + 2, 2);
			m_pic_cap_start_min = convert_ascii_int(data + 4, 2);
			sprintf(response_msg, "Picture start time at %i:%02i", m_pic_cap_start_hour, m_pic_cap_start_min);
		}
		// Set capture start time command
		else if(data[0] == 'c' && data[1] == 'e' && len == 6){
			m_pic_cap_end_hour = convert_ascii_int(data + 2, 2);
			m_pic_cap_end_min = convert_ascii_int(data + 4, 2);
			sprintf(response_msg, "Picture start time at %i:%02i", m_pic_cap_end_hour, m_pic_cap_end_min);
		}
		// Get current time command
		else if(data[0] == 'g' && data[1] == 't' && len == 2){
			static time_t t;
			static struct tm *ptr;
						// Check current time 
			t = time(NULL);
			ptr = localtime(&t);
			//time_update_requested = true;
			sprintf(response_msg, "Current time: %s", asctime(ptr));
		}
		else if(data[0] == 't' && data[1] == 'p' && len == 2){
			take_picture_requested = true;
			sprintf(response_msg, "Picture request received");
		}
		else sprintf(response_msg, "Unknown NUS command received!");
	}
	printk("%s\n", response_msg);
	send_nus_response_str(response_msg);
}

void on_nus_sent(struct bt_conn *conn)
{

}

void on_nus_send_enabled(enum bt_nus_send_status status)
{
	if(status == BT_NUS_SEND_STATUS_ENABLED) m_nus_notifications_enabled = true;
	else m_nus_notifications_enabled = false;
}

struct bt_nus_cb nus_callbacks = {.received = on_nus_received, .sent = on_nus_sent, .send_enabled = on_nus_send_enabled};

static void time_debug(void)
{
	static bool first_time = true;
	static struct tm* ptr;
	struct tm start_time;
    static time_t t;
	static time_t time_at_last_pic = 0;
	struct timespec ts;

	if(first_time) {
		first_time = false;
		start_time.tm_year = 122;
		start_time.tm_mon = 0;
		start_time.tm_mday = 24;
		start_time.tm_hour = 8;
		start_time.tm_min = 12;
		start_time.tm_sec = 12;
		t = mktime(&start_time);
		ts.tv_sec = t;
		ts.tv_nsec = 0;
		clock_settime(CLOCK_REALTIME, &ts);
		printk("Time set to: %s", asctime(&start_time));
	}

	// Check current time 
	t = time(NULL);
    ptr = localtime(&t);

	// Check if this is within the active picture period
	if(!m_time_set_from_app || (ptr->tm_hour >= m_pic_cap_start_hour && ptr->tm_hour <= m_pic_cap_end_hour && ptr->tm_wday >= 1 && ptr->tm_wday <= 5)) {
		if(m_time_set_from_app && ptr->tm_hour == m_pic_cap_start_hour && ptr->tm_min < m_pic_cap_start_min) return;
		if(m_time_set_from_app && ptr->tm_hour == m_pic_cap_end_hour && ptr->tm_min > m_pic_cap_end_min) return;
		if((t - time_at_last_pic) >= m_picture_interval_s) {
			time_at_last_pic = t;
			printk("Taking picture at time %s\n", asctime(ptr));
			cam_tl_control_take_picture();
		}
	}
}

void main(void)
{
	int blink_status = 0;
	int err;

	printk("Starting Camera timelapse control example\n");

	cam_tl_control_config_t cam_config = {.pin_focus = 24, .pin_shutter = 25};
	cam_tl_control_init(&cam_config);

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return;
	}

	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);
	if (IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED)) {
		bt_conn_auth_cb_register(&conn_auth_callbacks);
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_nus_init(&nus_callbacks);
	if (err) {
		printk("Failed to init NUS (err:%d)\n", err);
		return;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
	static char printbuf[128];
	static time_t t;
	static struct tm *ptr;
	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));

		// If the user button is pressed, take a picture
		if(take_picture_requested) {
			take_picture_requested = false;
			printk("Triggering picture manually\n");
			cam_tl_control_take_picture();
		}

		if(time_update_requested) {
			time_update_requested = false;
	
			// Check current time 
			t = time(NULL);
			ptr = localtime(&t);
			sprintf(printbuf, "Current time: %s", asctime(ptr));
			printk("Current time requested: %s\n", asctime(ptr));
			send_nus_response_str(printbuf);
		}

		time_debug();
	}
}
