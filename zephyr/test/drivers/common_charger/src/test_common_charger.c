/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "charge_ramp.h"
#include "driver/charger/isl923x_public.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

/* Tested wrt isl923x */

/* Only single charger-chip configured for the drivers overlay */
#define CHG_NUM get_charger_num(&isl923x_drv)

/*
 * Only faking functions that return an essentially constant value no chip
 * register reading and thus no emulator reading.
 */
FAKE_VALUE_FUNC(int, isl923x_ramp_is_stable, int);
FAKE_VALUE_FUNC(int, isl923x_ramp_is_detected, int);

ZTEST(common_charger, test_chg_ramp_is_stable)
{
	isl923x_ramp_is_stable_fake.return_val = 1;

	zassert_equal(chg_ramp_is_stable(),
		      isl923x_ramp_is_stable_fake.return_val);
	zassert_equal(isl923x_ramp_is_stable_fake.call_count, 1);
	zassert_equal(isl923x_ramp_is_stable_fake.arg0_val, CHG_NUM);
}

ZTEST(common_charger, test_chg_ramp_is_detected)
{
	isl923x_ramp_is_stable_fake.return_val = 0;

	zassert_equal(chg_ramp_is_detected(),
		      isl923x_ramp_is_detected_fake.return_val);
	zassert_equal(isl923x_ramp_is_detected_fake.call_count, 1);
	zassert_equal(isl923x_ramp_is_detected_fake.arg0_val, CHG_NUM);
}

ZTEST(common_charger, test_chg_ramp_get_current_limit)
{
	zassert_equal(chg_ramp_get_current_limit(),
		      CONFIG_CHARGER_INPUT_CURRENT);
}

ZTEST(common_charger, test_charger_is_icl_reached__bad_arg)
{
	bool unused = false;

	zassert_equal(charger_is_icl_reached(INT_MAX, &unused), EC_ERROR_INVAL);
	zassert_equal(charger_is_icl_reached(INT_MIN, &unused), EC_ERROR_INVAL);
}

ZTEST(common_charger, test_charger_is_icl_reached__unsupported)
{
	/* Not supported by isl923x */
	bool unused;

	zassert_equal(charger_is_icl_reached(CHG_NUM, &unused),
		      EC_ERROR_UNIMPLEMENTED);
}

static void suite_common_charger_before_after(void *test_data)
{
	ARG_UNUSED(test_data);

	RESET_FAKE(isl923x_ramp_is_stable);
	RESET_FAKE(isl923x_ramp_is_detected);
	/* Driver's default hard-coded value */
	isl923x_ramp_is_detected_fake.return_val = 1;

	isl923x_drv.init(CHG_NUM);
}

ZTEST_SUITE(common_charger, drivers_predicate_post_main, NULL,
	    suite_common_charger_before_after, NULL, NULL);