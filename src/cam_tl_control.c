#include "cam_tl_control.h"
#include "nrf.h"

static cam_tl_control_config_t m_local_config;

#define PIN_FOCUS_CLR() (NRF_GPIO->OUTSET = (1 << m_local_config.pin_focus))
#define PIN_FOCUS_SET() (NRF_GPIO->OUTCLR = (1 << m_local_config.pin_focus))
#define PIN_SHUTTER_CLR() (NRF_GPIO->OUTSET = (1 << m_local_config.pin_shutter))
#define PIN_SHUTTER_SET() (NRF_GPIO->OUTCLR = (1 << m_local_config.pin_shutter))


void cam_tl_control_init(cam_tl_control_config_t *config)
{
	m_local_config = *config;
	PIN_FOCUS_CLR();
	PIN_SHUTTER_CLR();
	NRF_GPIO->PIN_CNF[config->pin_focus] = 
		GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos |
		GPIO_PIN_CNF_DRIVE_H0D1 << GPIO_PIN_CNF_DRIVE_Pos;
	NRF_GPIO->PIN_CNF[config->pin_shutter] = 
		GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos |
		GPIO_PIN_CNF_DRIVE_H0D1 << GPIO_PIN_CNF_DRIVE_Pos;
}

void cam_tl_control_take_picture(void)
{
	PIN_FOCUS_SET();
	k_msleep(700);
	PIN_SHUTTER_SET();
	k_msleep(100);
	PIN_FOCUS_CLR();
	PIN_SHUTTER_CLR();
}