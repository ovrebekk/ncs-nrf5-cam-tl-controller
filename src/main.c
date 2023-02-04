/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>
#include <time.h>
#include <stdio.h>
/* clock_gettime() prototype */
#include <zephyr/posix/time.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/nus.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include "cam_tl_control.h"

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)


#define RUN_STATUS_LED          DK_LED1
#define CON_STATUS_LED          DK_LED2
#define RUN_LED_BLINK_INTERVAL  100

#define USER_LED                DK_LED3

#define USER_BUTTON             DK_BTN1_MSK
#define BLE_ENABLE_BUTTON       DK_BTN2_MSK

#define UART_BUF_SIZE CONFIG_BT_NUS_UART_BUFFER_SIZE

// Until the kit is configured through the app, run at a constant interval (since accurate time is not set)
static bool m_time_set_from_app = false;
static int m_picture_interval_s = 10*60;
static int m_pic_cap_start_hour = 8;
static int m_pic_cap_start_min = 0;
static int m_pic_cap_end_hour = 17;
static int m_pic_cap_end_min = 59;
static bool m_nus_notifications_enabled = false;

static int m_pics_taken_since_reset = 0;
static int m_pics_taken_since_last_ble_command = 0;

static struct bt_gatt_exchange_params exchange_params;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

struct bt_conn *m_conn = 0;

static void app_bt_start_advertising(bool enable) 
{
	int err;
	if(enable) {
		err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
					sd, ARRAY_SIZE(sd));
		if (err) {
			printk("Advertising failed to start (err %d)\n", err);
			return;
		}
	} else {
		err = bt_le_adv_stop();
	}
}

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

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
#endif

static volatile bool take_picture_override = false;
static volatile bool ble_enabled = true;
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON) {
		uint32_t user_button_state = button_state & USER_BUTTON;

		if(user_button_state) take_picture_override = true;
	}
	if ((has_changed & BLE_ENABLE_BUTTON) && (button_state & BLE_ENABLE_BUTTON)) {
		ble_enabled = !ble_enabled;
		if(ble_enabled) {
			printk("BLE Enabled\n");
		} else {
			printk("BLE Disabled\n");
		}
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

typedef struct {
	uint8_t buf[24];
	uint32_t len;
} uart_message_t;
K_MSGQ_DEFINE(nus_msg_queue, sizeof(uart_message_t), 8, 4);

void on_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	static uart_message_t new_message;
	if(len > 23) len = 23;
	memcpy(new_message.buf, data, len);
	new_message.buf[len] = 0;
	new_message.len = len;
	k_msgq_put(&nus_msg_queue, &new_message, K_NO_WAIT);
}

#define CHECK_CAM_CMD(a, b) (strncmp(a, msg->buf, 2) == 0 && msg->len == b)

static void process_nus_packet(uart_message_t *msg)
{
	struct tm set_time;
	static char response_msg[128];
	//printk("NUS CMD received (len %i): %s\n", len, response_msg);
	if(msg->len >= 2){
		// Set time command
		if(CHECK_CAM_CMD("st", 14)){
			set_time.tm_year = convert_ascii_int(msg->buf + 2, 2) + 100;
			set_time.tm_mon = convert_ascii_int(msg->buf + 4, 2);
			set_time.tm_mday = convert_ascii_int(msg->buf + 6, 2);
			set_time.tm_hour = convert_ascii_int(msg->buf + 8, 2);
			set_time.tm_min = convert_ascii_int(msg->buf + 10, 2);
			set_time.tm_sec = convert_ascii_int(msg->buf + 12, 2);
			time_t t = mktime(&set_time);
			struct timespec ts;
			ts.tv_sec = t;
			ts.tv_nsec = 0;
			clock_settime(CLOCK_REALTIME, &ts);
			m_time_set_from_app = true;
			sprintf(response_msg, "Time set over NUS: %s", asctime(&set_time));
		}
		// Set capture interval command
		else if(CHECK_CAM_CMD("si", 6)){
			m_picture_interval_s = convert_ascii_int(msg->buf + 2, 4);
			if(m_picture_interval_s < 30) m_picture_interval_s = 30;
			sprintf(response_msg, "Picture interval set to %i", m_picture_interval_s);
		}
		// Set capture start time command
		else if(CHECK_CAM_CMD("cs", 6)){
			m_pic_cap_start_hour = convert_ascii_int(msg->buf + 2, 2);
			m_pic_cap_start_min = convert_ascii_int(msg->buf + 4, 2);
			sprintf(response_msg, "Picture start time at %i:%02i", m_pic_cap_start_hour, m_pic_cap_start_min);
		}
		// Set capture end time command
		else if(CHECK_CAM_CMD("ce", 6)){
			m_pic_cap_end_hour = convert_ascii_int(msg->buf + 2, 2);
			m_pic_cap_end_min = convert_ascii_int(msg->buf + 4, 2);
			sprintf(response_msg, "Picture end time at %i:%02i", m_pic_cap_end_hour, m_pic_cap_end_min);
		}
		// Get current time command
		else if(CHECK_CAM_CMD("gt", 2)){
			static time_t t;
			static struct tm *ptr;
			
			// Check current time 
			t = time(NULL);
			ptr = localtime(&t);
			sprintf(response_msg, "Current time: %s", asctime(ptr));
		}
		// Get status command
		else if(CHECK_CAM_CMD("gs", 2)){
			sprintf(response_msg, "Picture start time at %i:%02i", m_pic_cap_start_hour, m_pic_cap_start_min);
			send_nus_response_str(response_msg);
			sprintf(response_msg, "Picture end time at %i:%02i", m_pic_cap_end_hour, m_pic_cap_end_min);
			send_nus_response_str(response_msg);
			sprintf(response_msg, "Picture interval: %i", m_picture_interval_s);
			send_nus_response_str(response_msg);
			sprintf(response_msg, "Pics since reset: %i, pics since BLE activity: %i", m_pics_taken_since_reset, m_pics_taken_since_last_ble_command);
			send_nus_response_str(response_msg);
			response_msg[0] = 0;
		}
		else if(CHECK_CAM_CMD("tp", 2)){
			take_picture_requested = true;
			sprintf(response_msg, "Picture request received");
		}
		else sprintf(response_msg, "Unknown NUS command received!");
	}
	printk("%s\n", response_msg);
	send_nus_response_str(response_msg);
	m_pics_taken_since_last_ble_command = 0;
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
			m_pics_taken_since_reset++;
			m_pics_taken_since_last_ble_command++;
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

	app_bt_start_advertising(true);

	printk("Advertising successfully started\n");
	static char printbuf[128];
	static time_t t;
	static struct tm *ptr;
	uint32_t time_debug_counter = 0;
	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));

		// If the user button is pressed, take a picture
		if(take_picture_requested) {
			take_picture_requested = false;
			printk("Triggering picture manually\n");
			cam_tl_control_take_picture();
			m_pics_taken_since_reset++;
			m_pics_taken_since_last_ble_command++;
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

		if(time_debug_counter > 1000) {
			time_debug();
			time_debug_counter -= 1000;
		}
		time_debug_counter += RUN_LED_BLINK_INTERVAL;
		
		static uart_message_t new_msg;
		if(k_msgq_get(&nus_msg_queue, &new_msg, K_NO_WAIT) == 0) {
			process_nus_packet(&new_msg);
		}
	}
}
