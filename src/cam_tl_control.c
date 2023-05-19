#include "cam_tl_control.h"
#include "nrf.h"
#include <zephyr/drivers/gpio.h>

struct gpio_dt_spec pin_focus = GPIO_DT_SPEC_GET(DT_NODELABEL(pin_focus), gpios);
struct gpio_dt_spec pin_shutter = GPIO_DT_SPEC_GET(DT_NODELABEL(pin_shutter), gpios);

int cam_tl_control_init()
{
	int ret;

	if(!device_is_ready(pin_focus.port)) {
		return -ENXIO;
	}

	if(!device_is_ready(pin_shutter.port)) {
		return -ENXIO;
	}

	ret = gpio_pin_configure_dt(&pin_focus, GPIO_OUTPUT);
	if (ret) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&pin_shutter, GPIO_OUTPUT);
	if (ret) {
		return ret;
	}

	gpio_pin_set_dt(&pin_focus, 1);
	gpio_pin_set_dt(&pin_shutter, 1);

	return 0;
}

void cam_tl_control_take_picture(void)
{
	gpio_pin_set_dt(&pin_focus, 0);
	k_msleep(700);
	gpio_pin_set_dt(&pin_shutter, 0);
	k_msleep(100);
	gpio_pin_set_dt(&pin_focus, 1);
	gpio_pin_set_dt(&pin_shutter, 1);
}