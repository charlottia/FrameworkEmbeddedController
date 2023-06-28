/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"
#include "board_host_command.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "input_module.h"
#include "task.h"
#include "util.h"
#include "zephyr_console_shim.h"
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "board_adc.h"
#include "flash_storage.h"

LOG_MODULE_REGISTER(inputmodule, LOG_LEVEL_INF);

#define INPUT_MODULE_POWER_ON_DELAY (2)
#define INPUT_MODULE_MUX_DELAY_US 2

int oc_count;
int force_on;
int detect_mode;
int hub_board_id[8];	/* EC console Debug use */
enum input_deck_state deck_state;

void module_oc_interrupt(enum gpio_signal signal)
{
    oc_count++;
}

void set_detect_mode(int mode)
{
	detect_mode = mode;
}

int get_detect_mode(void)
{
	return detect_mode;
}


static void set_hub_mux(uint8_t input)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a0),
		((input & BIT(0)) >> 0));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a1),
		((input & BIT(1)) >> 1));
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_mux_a2),
		((input & BIT(2)) >> 2));
}

static void scan_c_deck(bool full_scan)
{
    int i;
	if (full_scan) {
		for (i = 0; i < 8; i++) {
			/* Switch the mux */
			set_hub_mux(i);
			/*
			 * In the specification table Switching Characteristics over Operating
			 * range the maximum Bus Select Time needs 6.6 ns, so delay a little
			 */
			usleep(INPUT_MODULE_MUX_DELAY_US);

			hub_board_id[i] = get_hardware_id(ADC_HUB_BOARD_ID);
		}
	} else {
		set_hub_mux(TOUCHPAD);
		usleep(INPUT_MODULE_MUX_DELAY_US);
		hub_board_id[TOUCHPAD] = get_hardware_id(ADC_HUB_BOARD_ID);
	}
	/* Turn off hub mux pins*/
	set_hub_mux(TOP_ROW_NOT_CONNECTED);
}

static void board_input_module_init(void)
{
	/* need to wait bios_function_init() to update detect mode */
	if (detect_mode == 0x02)
		deck_state = DECK_FORCE_ON;
	else if (detect_mode == 0x04)
		deck_state = DECK_FORCE_OFF;
	else
		deck_state = DECK_OFF;
}
DECLARE_HOOK(HOOK_INIT, board_input_module_init, HOOK_PRIO_DEFAULT + 2);

static void poll_c_deck(void)
{
	static int turning_on_count;
	
	switch (deck_state) {
	case DECK_OFF:
		break;
	case DECK_DISCONNECTED:
		scan_c_deck(true);
		/* TODO only poll touchpad and currently connected B1/C1 modules
		 * if c deck state is ON as these must be removed first
		 */
		if (hub_board_id[TOUCHPAD] == INPUT_MODULE_TOUCHPAD) {
			turning_on_count = 0;
			deck_state = DECK_TURNING_ON;
		}
		break;
	case DECK_TURNING_ON:
		turning_on_count++;
		scan_c_deck(false);
		if (hub_board_id[TOUCHPAD] == INPUT_MODULE_TOUCHPAD &&
			turning_on_count > INPUT_MODULE_POWER_ON_DELAY) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
			deck_state = DECK_ON;
			LOG_INF("Input modules on");
		} else if (hub_board_id[TOUCHPAD] != INPUT_MODULE_TOUCHPAD) {
			deck_state = DECK_DISCONNECTED;
		}
		break;
	case DECK_ON:
		/* TODO Add lid detection,
		 * if lid is closed input modules cannot be removed
		 */

		scan_c_deck(false);
		if (hub_board_id[TOUCHPAD] > INPUT_MODULE_TOUCHPAD) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
			deck_state = DECK_DISCONNECTED;
			LOG_INF("Input modules off");
		}
		break;
	case DECK_FORCE_ON:
	case DECK_FORCE_OFF:
	default:
		break;
	}
}
DECLARE_HOOK(HOOK_TICK, poll_c_deck, HOOK_PRIO_DEFAULT);

static void input_modules_powerup(void)
{
	if (deck_state == DECK_FORCE_ON)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
	else if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_ON)
		deck_state = DECK_DISCONNECTED;

}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, input_modules_powerup, HOOK_PRIO_DEFAULT);

void input_modules_powerdown(void)
{
	if (deck_state == DECK_FORCE_ON)
		 gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
	else if (deck_state != DECK_FORCE_ON && deck_state != DECK_FORCE_ON) {
		deck_state = DECK_OFF;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
		/* Hub mux input 6 is NC, so lower power draw  by disconnecting all PD */
		set_hub_mux(TOP_ROW_NOT_CONNECTED);
	}
}

int get_deck_state(void)
{
	return deck_state;
}

/* Host command */
static enum ec_status check_deck_state(struct host_cmd_handler_args *args)
{
	const struct ec_params_deck_state *p = args->params;
	struct ec_response_deck_state *r = args->response;
	int idx;

	/* set mode */
	if (p->mode == 0x01)
		deck_state = DECK_DISCONNECTED;
	else if (p->mode == 0x02)
		deck_state = DECK_FORCE_ON;
	else if (p->mode == 0x04)
		deck_state = DECK_FORCE_OFF;

	set_detect_mode(p->mode);

	/* return deck status */
	for (idx = 0; idx < 8; idx++)
		r->input_deck_board_id[idx] = (uint8_t)hub_board_id[idx];

	r->deck_state = deck_state;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHECK_DECK_STATE, check_deck_state, EC_VER_MASK(0));

/* EC console command */
static int inputdeck_cmd(int argc, const char **argv)
{
	int i, mv, id;
	static const char * const deck_states[] = {
		"OFF", "DISCONNECTED", "TURNING_ON", "ON", "FORCE_OFF", "FORCE_ON", "NO_DETECTION"
	};

	if (argc >= 2) {
		if (!strncmp(argv[1], "on", 2)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 1);
			ccprintf("Forcing Input modules on\n");
			deck_state = DECK_FORCE_ON;
		} else if (!strncmp(argv[1], "off", 3)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
			deck_state = DECK_FORCE_OFF;
		} else if (!strncmp(argv[1], "auto", 4)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
			deck_state = DECK_DISCONNECTED;
		} else if (!strncmp(argv[1], "nodetection", 4)) {
			deck_state = DECK_NO_DETECTION;
		}
	}
	scan_c_deck(true);
	ccprintf("Deck state: %s\n", deck_states[deck_state]);
	for (i = 0; i < 8; i++) {
			/* Switch the mux */
			set_hub_mux(i);
			/*
			 * In the specification table Switching Characteristics over Operating
			 * range the maximum Bus Select Time needs 6.6 ns, so delay a little
			 */
			usleep(INPUT_MODULE_MUX_DELAY_US);

			id = get_hardware_id(ADC_HUB_BOARD_ID);
			mv = adc_read_channel(ADC_HUB_BOARD_ID);
			ccprintf("    C deck status %d = %d %d mv\n", i, id, mv);

		}

	ccprintf("Input module Overcurrent Events: %d\n", oc_count);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(inputdeck, inputdeck_cmd, "[on/off/auto/nodetection]",
			"Input modules power sequence control");
