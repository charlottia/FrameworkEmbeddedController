/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charger functions related to a connected keyboard called a 'base' */

#ifndef __CROS_EC_CHARGER_BASE_H
#define __CROS_EC_CHARGER_BASE_H

#include <stdbool.h>

struct charge_state_data;

extern int base_responsive;
extern int charge_base;
extern int prev_charge_base;

/* allocate power between the base and the lid */
void base_charge_allocate_input_current_limit(
	const struct charge_state_data *curr, bool is_full, bool debugging);

/*
 * Check base external-power settings and react as needed
 *
 * @param ac Current ac value from struct charge_state_data
 * @param prev_ac Previous value of ac
 * Returns true if ac should be zeroed, false to leave it along
 */
bool base_check_extpower(int ac, int prev_ac);

/* Update base battery information */
void base_update_battery_info(void);

#ifdef CONFIG_EC_EC_COMM_BATTERY_CLIENT
/* Check if there is a base and it is connected */
bool base_connected(void);
#else
bool base_connected(void)
{
	return false;
}
#endif

#endif /* __CROS_EC_CHARGER_BASE_H */
