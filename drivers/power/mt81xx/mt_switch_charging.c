
#include <linux/kernel.h>
/* #include <linux/musb/musb_core.h> */
#include "mt_charging.h"
#include <mt_boot.h>
#include "mt_battery_meter.h"
#include "mt_battery_custom_data.h"
#include "mt_battery_common.h"

/* TODO: temp code for usb!!! */

enum usb_state_enum {
	USB_SUSPEND = 0,
	USB_UNCONFIGURED,
	USB_CONFIGURED
};


 /* ============================================================ // */
 /* define */
 /* ============================================================ // */
 /* cut off to full */
#define POST_CHARGING_TIME	 (30 * 60)	/* 30mins */
#define FULL_CHECK_TIMES		6

 /* ============================================================ // */
 /* global variable */
 /* ============================================================ // */
u32 g_bcct_flag = 0;
u32 g_bcct_value = 0;

int g_temp_CC_value = CHARGE_CURRENT_0_00_MA;
int g_temp_input_CC_value = CHARGE_CURRENT_0_00_MA;
u32 g_usb_state = USB_UNCONFIGURED;
static u32 full_check_count;

  /* ///////////////////////////////////////////////////////////////////////////////////////// */
  /* // JEITA */
  /* ///////////////////////////////////////////////////////////////////////////////////////// */
#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
int g_temp_status = TEMP_POS_10_TO_POS_45;
bool temp_error_recovery_chr_flag = true;
bool trickle_charge_stage = false;
#endif

 /* ============================================================ // */
void BATTERY_SetUSBState(int usb_state_value)
{
#if defined(CONFIG_POWER_EXT)
	battery_log(BAT_LOG_CRTI, "[BATTERY_SetUSBState] in FPGA/EVB, no service\r\n");
#else
	if ((usb_state_value < USB_SUSPEND) || ((usb_state_value > USB_CONFIGURED))) {
		battery_log(BAT_LOG_CRTI,
			    "[BATTERY] BAT_SetUSBState Fail! Restore to default value\r\n");
		usb_state_value = USB_UNCONFIGURED;
	} else {
		battery_log(BAT_LOG_CRTI, "[BATTERY] BAT_SetUSBState Success! Set %d\r\n",
			    usb_state_value);
		g_usb_state = usb_state_value;
	}
#endif
}

void bat_charger_update_usb_state(int usb_state)
{
	BATTERY_SetUSBState(usb_state);
	wake_up_bat();
}
EXPORT_SYMBOL(bat_charger_update_usb_state);

u32 get_charging_setting_current(void)
{
	return g_temp_CC_value;
}


#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)

static int select_jeita_cv(void)
{
	int cv_voltage;

	if (g_temp_status == TEMP_ABOVE_POS_60)
		cv_voltage = p_bat_charging_data->jeita_temp_above_pos_60_cv_voltage;
	else if (g_temp_status == TEMP_POS_45_TO_POS_60)
		cv_voltage = p_bat_charging_data->jeita_temp_pos_45_to_pos_60_cv_voltage;
	else if (g_temp_status == TEMP_POS_10_TO_POS_45)
		cv_voltage = p_bat_charging_data->jeita_temp_pos_10_to_pos_45_cv_voltage;
	else if (g_temp_status == TEMP_POS_0_TO_POS_10)
		cv_voltage = p_bat_charging_data->jeita_temp_pos_0_to_pos_10_cv_voltage;
	else if (g_temp_status == TEMP_NEG_10_TO_POS_0)
		cv_voltage = p_bat_charging_data->jeita_temp_neg_10_to_pos_0_cv_voltage;
	else if (g_temp_status == TEMP_BELOW_NEG_10)
		cv_voltage = p_bat_charging_data->jeita_temp_below_neg_10_cv_voltage;
	else
		cv_voltage = BATTERY_VOLT_04_200000_V;

	if (g_temp_status == TEMP_POS_0_TO_POS_10 && trickle_charge_stage == true)
		cv_voltage = BATTERY_VOLT_04_200000_V;

	return cv_voltage;
}

int do_jeita_state_machine(void)
{
	int previous_g_temp_status;
	int cv_voltage;

	/* JEITA battery temp Standard */
	previous_g_temp_status = g_temp_status;

	if (BMT_status.temperature >= p_bat_charging_data->temp_pos_60_threshold) {
		battery_log(BAT_LOG_CRTI,
			    "[BATTERY] Battery Over high Temperature(%d) !!\n\r",
			    p_bat_charging_data->temp_pos_60_threshold);

		g_temp_status = TEMP_ABOVE_POS_60;

		return PMU_STATUS_FAIL;
	} else if (BMT_status.temperature > p_bat_charging_data->temp_pos_45_threshold) {
		if ((g_temp_status == TEMP_ABOVE_POS_60)
		    && (BMT_status.temperature >= p_bat_charging_data->temp_pos_60_thres_minus_x_degree)) {
			battery_log(BAT_LOG_CRTI,
				"[BATTERY] Battery Temperature between %d and %d,not allow charging yet!!\n\r",
				p_bat_charging_data->temp_pos_60_thres_minus_x_degree,
				p_bat_charging_data->temp_pos_60_threshold);

			return PMU_STATUS_FAIL;
		}
		battery_log(BAT_LOG_CRTI,
			"[BATTERY] Battery Temperature between %d and %d !!\n\r",
			p_bat_charging_data->temp_pos_45_threshold, p_bat_charging_data->temp_pos_60_threshold);

		g_temp_status = TEMP_POS_45_TO_POS_60;

	} else if (BMT_status.temperature >= p_bat_charging_data->temp_pos_10_threshold) {
		if (((g_temp_status == TEMP_POS_45_TO_POS_60)
		     && (BMT_status.temperature >= p_bat_charging_data->temp_pos_45_thres_minus_x_degree))
		    || ((g_temp_status == TEMP_POS_0_TO_POS_10)
			&& (BMT_status.temperature <= p_bat_charging_data->temp_pos_10_thres_plus_x_degree))) {
			battery_log(BAT_LOG_CRTI,
				    "[BATTERY] Battery Temperature not recovery to normal temperature charging mode yet!!\n\r");
		} else {
			battery_log(BAT_LOG_CRTI,
				    "[BATTERY] Battery Normal Temperature between %d and %d !!\n\r",
				    p_bat_charging_data->temp_pos_10_threshold,
				    p_bat_charging_data->temp_pos_45_threshold);
			g_temp_status = TEMP_POS_10_TO_POS_45;
		}
	} else if (BMT_status.temperature >= p_bat_charging_data->temp_pos_0_threshold) {
		if ((g_temp_status == TEMP_NEG_10_TO_POS_0 || g_temp_status == TEMP_BELOW_NEG_10)
		    && (BMT_status.temperature <= p_bat_charging_data->temp_pos_0_thres_plus_x_degree)) {
			if (g_temp_status == TEMP_BELOW_NEG_10
			    || g_temp_status == TEMP_NEG_10_TO_POS_0) {
				battery_log(BAT_LOG_CRTI,
					"[BATTERY] Battery Temperature between %d and %d,not allow charging yet!!\n\r",
					p_bat_charging_data->temp_pos_0_threshold,
					p_bat_charging_data->temp_pos_0_thres_plus_x_degree);
				return PMU_STATUS_FAIL;
			}
		} else {
			battery_log(BAT_LOG_CRTI,
				    "[BATTERY] Battery Temperature between %d and %d !!\n\r",
				    p_bat_charging_data->temp_pos_0_threshold,
				    p_bat_charging_data->temp_pos_10_threshold);

			g_temp_status = TEMP_POS_0_TO_POS_10;
		}
	} else if (BMT_status.temperature >= p_bat_charging_data->temp_neg_10_threshold) {
		if ((g_temp_status == TEMP_BELOW_NEG_10)
		    && (BMT_status.temperature <= p_bat_charging_data->temp_neg_10_thres_plus_x_degree)) {
			battery_log(BAT_LOG_CRTI,
				    "[BATTERY] Battery Temperature between %d and %d,not allow charging yet!!\n\r",
				    p_bat_charging_data->temp_neg_10_threshold,
				    p_bat_charging_data->temp_neg_10_thres_plus_x_degree);

			return PMU_STATUS_FAIL;
		}
		battery_log(BAT_LOG_CRTI,
			    "[BATTERY] Battery Temperature between %d and %d,not allow charging yet!!\n\r",
			    p_bat_charging_data->temp_neg_10_threshold, p_bat_charging_data->temp_pos_0_threshold);

		g_temp_status = TEMP_NEG_10_TO_POS_0;
		return PMU_STATUS_FAIL;

	} else {
		battery_log(BAT_LOG_CRTI,
			    "[BATTERY] Battery below low Temperature(%d) !!\n\r",
			    p_bat_charging_data->temp_neg_10_threshold);
		g_temp_status = TEMP_BELOW_NEG_10;

		return PMU_STATUS_FAIL;
	}

	/* set CV after temperature changed */
	if (g_temp_status != previous_g_temp_status) {
		cv_voltage = select_jeita_cv();
		bat_charger_set_cv_voltage(cv_voltage);
	}

	return PMU_STATUS_OK;
}


static void set_jeita_charging_current(void)
{
#ifdef CONFIG_CONFIG_USB_IF
	if (BMT_status.charger_type == STANDARD_HOST)
		return;
#endif

	if (g_temp_status == TEMP_NEG_10_TO_POS_0) {
		g_temp_CC_value = CHARGE_CURRENT_350_00_MA;
		g_temp_input_CC_value = CHARGE_CURRENT_500_00_MA;
		battery_log(BAT_LOG_CRTI, "[BATTERY] JEITA set charging current : %d\r\n",
			    g_temp_CC_value);
	} else if (g_temp_status == TEMP_POS_0_TO_POS_10) {
		g_temp_CC_value = 51200;
		if (trickle_charge_stage == true)
			g_temp_CC_value = 25600;
	} else if (g_temp_status == TEMP_POS_10_TO_POS_45) {
		if (BMT_status.temperature <= 23)
			g_temp_CC_value = 153600;
		else
			g_temp_CC_value = 211200;
	} else {
		g_temp_CC_value = 153600;
	}
}

#endif


void select_charging_curret_bcct(void)
{
	if ((BMT_status.charger_type == STANDARD_HOST) ||
	    (BMT_status.charger_type == NONSTANDARD_CHARGER)) {
		if (g_bcct_value < 100)
			g_temp_input_CC_value = CHARGE_CURRENT_0_00_MA;
		else if (g_bcct_value < 500)
			g_temp_input_CC_value = CHARGE_CURRENT_100_00_MA;
		else if (g_bcct_value < 800)
			g_temp_input_CC_value = CHARGE_CURRENT_500_00_MA;
		else if (g_bcct_value == 800)
			g_temp_input_CC_value = CHARGE_CURRENT_800_00_MA;
		else
			g_temp_input_CC_value = CHARGE_CURRENT_500_00_MA;
	} else if ((BMT_status.charger_type == STANDARD_CHARGER)
		   || (BMT_status.charger_type == APPLE_1_0A_CHARGER)
		   || (BMT_status.charger_type == APPLE_2_1A_CHARGER)
		   || (BMT_status.charger_type == CHARGING_HOST)) {
		g_temp_input_CC_value = CHARGE_CURRENT_MAX;

		/* --------------------------------------------------- */
		/* set IOCHARGE */
		if (g_bcct_value < 550)
			g_temp_CC_value = CHARGE_CURRENT_0_00_MA;
		else if (g_bcct_value < 650)
			g_temp_CC_value = CHARGE_CURRENT_550_00_MA;
		else if (g_bcct_value < 750)
			g_temp_CC_value = CHARGE_CURRENT_650_00_MA;
		else if (g_bcct_value < 850)
			g_temp_CC_value = CHARGE_CURRENT_750_00_MA;
		else if (g_bcct_value < 950)
			g_temp_CC_value = CHARGE_CURRENT_850_00_MA;
		else if (g_bcct_value < 1050)
			g_temp_CC_value = CHARGE_CURRENT_950_00_MA;
		else if (g_bcct_value < 1150)
			g_temp_CC_value = CHARGE_CURRENT_1050_00_MA;
		else if (g_bcct_value < 1250)
			g_temp_CC_value = CHARGE_CURRENT_1150_00_MA;
		else if (g_bcct_value == 1250)
			g_temp_CC_value = CHARGE_CURRENT_1250_00_MA;
		else
			g_temp_CC_value = CHARGE_CURRENT_650_00_MA;
		/* --------------------------------------------------- */

	} else {
		g_temp_input_CC_value = CHARGE_CURRENT_500_00_MA;
	}
}


u32 set_bat_charging_current_limit(int current_limit)
{

	if (current_limit != -1) {
		g_bcct_flag = 1;
		g_bcct_value = current_limit * 100;

	} else {
		/* change to default current setting */
		g_bcct_flag = 0;
	}

	return g_bcct_flag;
}


void select_charging_curret(void)
{
	if (g_ftm_battery_flag) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] FTM charging : %d\r\n",
			    charging_level_data[0]);
		g_temp_CC_value = charging_level_data[0];

		if (g_temp_CC_value == CHARGE_CURRENT_450_00_MA) {
			g_temp_input_CC_value = CHARGE_CURRENT_500_00_MA;
		} else {
			g_temp_input_CC_value = CHARGE_CURRENT_MAX;
			g_temp_CC_value = p_bat_charging_data->ac_charger_current;

			battery_log(BAT_LOG_CRTI, "[BATTERY] set_ac_current \r\n");
		}
	} else if (g_custom_charging_current != -1) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] custom charging : %d\r\n",
			    g_custom_charging_current);
		g_temp_CC_value = g_custom_charging_current;

		if (g_temp_CC_value <= CHARGE_CURRENT_500_00_MA)
			g_temp_input_CC_value = CHARGE_CURRENT_500_00_MA;
		else
			g_temp_input_CC_value = CHARGE_CURRENT_MAX;

	} else {
		if (BMT_status.charger_type == STANDARD_HOST) {
#ifdef CONFIG_CONFIG_USB_IF
			{
				g_temp_input_CC_value = CHARGE_CURRENT_MAX;
				if (g_usb_state == USB_SUSPEND)
					g_temp_CC_value = USB_CHARGER_CURRENT_SUSPEND;
				else if (g_usb_state == USB_UNCONFIGURED)
					g_temp_CC_value = USB_CHARGER_CURRENT_UNCONFIGURED;
				else if (g_usb_state == USB_CONFIGURED)
					g_temp_CC_value = USB_CHARGER_CURRENT_CONFIGURED;
				else
					g_temp_CC_value = USB_CHARGER_CURRENT_UNCONFIGURED;

				battery_log(BAT_LOG_CRTI,
					    "[BATTERY] STANDARD_HOST CC mode charging : %d on %d state\r\n",
					    g_temp_CC_value, g_usb_state);
			}
#else
			{
				g_temp_input_CC_value = p_bat_charging_data->usb_charger_current;
				g_temp_CC_value = p_bat_charging_data->usb_charger_current;
			}
#endif
		} else if (BMT_status.charger_type == NONSTANDARD_CHARGER) {
			g_temp_input_CC_value = p_bat_charging_data->non_std_ac_charger_current;
			g_temp_CC_value = p_bat_charging_data->non_std_ac_charger_current;

		} else if (BMT_status.charger_type == STANDARD_CHARGER) {
			g_temp_input_CC_value = p_bat_charging_data->ac_charger_current;
			g_temp_CC_value = p_bat_charging_data->ac_charger_current;
		} else if (BMT_status.charger_type == CHARGING_HOST) {
			g_temp_input_CC_value = p_bat_charging_data->charging_host_charger_current;
			g_temp_CC_value = p_bat_charging_data->charging_host_charger_current;
		} else if (BMT_status.charger_type == APPLE_2_1A_CHARGER) {
			g_temp_input_CC_value = p_bat_charging_data->apple_2_1a_charger_current;
			g_temp_CC_value = p_bat_charging_data->apple_2_1a_charger_current;
		} else if (BMT_status.charger_type == APPLE_1_0A_CHARGER) {
			g_temp_input_CC_value = p_bat_charging_data->apple_1_0a_charger_current;
			g_temp_CC_value = p_bat_charging_data->apple_1_0a_charger_current;
		} else if (BMT_status.charger_type == APPLE_0_5A_CHARGER) {
			g_temp_input_CC_value = p_bat_charging_data->apple_0_5a_charger_current;
			g_temp_CC_value = p_bat_charging_data->apple_0_5a_charger_current;
		} else {
			g_temp_input_CC_value = CHARGE_CURRENT_500_00_MA;
			g_temp_CC_value = CHARGE_CURRENT_500_00_MA;
		}

		battery_log(BAT_LOG_FULL,
			    "[BATTERY] Default CC mode charging : %d, input current = %d\r\n",
			    g_temp_CC_value, g_temp_input_CC_value);

#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
		set_jeita_charging_current();
#endif
	}


}

static bool charging_full_check(void)
{
	bool status = bat_charger_get_charging_status();

	if (status) {
		full_check_count++;
		if (full_check_count >= FULL_CHECK_TIMES)
			return true;
		else
			return false;
	} else
		full_check_count = 0;

	return status;
}

static void pchr_turn_on_charging(void)
{
	int cv_voltage;
	bool charging_enable = true;

	if (BMT_status.bat_charging_state == CHR_ERROR) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charger Error, turn OFF charging !\n");

		charging_enable = false;

	} else if ((g_platform_boot_mode == META_BOOT) || (g_platform_boot_mode == ADVMETA_BOOT)) {
		battery_log(BAT_LOG_CRTI,
			    "[BATTERY] In meta or advanced meta mode, disable charging.\n");
		charging_enable = false;
	} else {
		/*HW initialization */

		bat_charger_init(p_bat_charging_data);

		battery_log(BAT_LOG_FULL, "charging_hw_init\n");

		/* Set Charging Current */
		select_charging_curret();
		battery_log(BAT_LOG_FULL, "[BATTERY] select_charging_curret !\n");

		if (g_bcct_flag == 1) {
			if (g_bcct_value < g_temp_CC_value)
				g_temp_CC_value = g_bcct_value;

			battery_log(BAT_LOG_FULL, "[BATTERY] select_charging_curret_bcct !\n");
		}

		if (g_temp_CC_value == CHARGE_CURRENT_0_00_MA
		    || g_temp_input_CC_value == CHARGE_CURRENT_0_00_MA) {

			charging_enable = false;

			battery_log(BAT_LOG_CRTI,
				    "[BATTERY] charging current is set 0mA, turn off charging !\r\n");
		} else {
			bat_charger_set_input_current(g_temp_input_CC_value);
			bat_charger_set_current(g_temp_CC_value);

			/*Set CV Voltage */
#if !defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
#ifdef CONFIG_HIGH_BATTERY_VOLTAGE_SUPPORT
			cv_voltage = BATTERY_VOLT_04_340000_V;
#else
			cv_voltage = BATTERY_VOLT_04_200000_V;
#endif
#else
			cv_voltage = select_jeita_cv();
#endif
			bat_charger_set_cv_voltage(cv_voltage);
		}
	}

	/* enable/disable charging */
	bat_charger_enable(charging_enable);

	battery_log(BAT_LOG_FULL, "[BATTERY] pchr_turn_on_charging(), enable =%d !\r\n",
		    charging_enable);
}


int BAT_PreChargeModeAction(void)
{
	battery_log(BAT_LOG_FULL, "[BATTERY] Pre-CC mode charge, timer=%d on %d !!\n\r",
		    BMT_status.PRE_charging_time, BMT_status.total_charging_time);

	BMT_status.PRE_charging_time += BAT_TASK_PERIOD;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.total_charging_time += BAT_TASK_PERIOD;

#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
	trickle_charge_stage = false;
#endif

	/*  Enable charger */
	pchr_turn_on_charging();

	if (BMT_status.UI_SOC == 100) {
		BMT_status.bat_charging_state = CHR_BATFULL;
		BMT_status.bat_full = true;
		g_charging_full_reset_bat_meter = true;
	} else if (BMT_status.bat_vol > p_bat_charging_data->v_pre2cc_thres) {
		BMT_status.bat_charging_state = CHR_CC;
	}



	return PMU_STATUS_OK;
}


int BAT_ConstantCurrentModeAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] CC mode charge, timer=%d on %d !!\n\r",
		    BMT_status.CC_charging_time, BMT_status.total_charging_time);

	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time += BAT_TASK_PERIOD;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.total_charging_time += BAT_TASK_PERIOD;

	/*  Enable charger */
	pchr_turn_on_charging();



	if (charging_full_check() == true) {

#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
		if (g_temp_status == TEMP_POS_0_TO_POS_10) {
			if (trickle_charge_stage == false) {
				trickle_charge_stage = true;
				full_check_count = 0;
				return PMU_STATUS_OK;
			}
		} else
			trickle_charge_stage = false;
#endif
		BMT_status.bat_charging_state = CHR_BATFULL;
		BMT_status.bat_full = true;
		g_charging_full_reset_bat_meter = true;
	}

	return PMU_STATUS_OK;
}


int BAT_BatteryFullAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Battery full !!\n\r");

	BMT_status.bat_full = true;
	BMT_status.total_charging_time = 0;
	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.POSTFULL_charging_time = 0;
	BMT_status.bat_in_recharging_state = false;

	if (charging_full_check() == false) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery Re-charging !!\n\r");

		if (BMT_status.bat_in_recharging_state == true) {
			if (BMT_status.UI_SOC < 100) {
				BMT_status.bat_in_recharging_state = false;
				BMT_status.bat_charging_state = CHR_CC;
				BMT_status.bat_full = false;
				return PMU_STATUS_OK;
			}
		}

		BMT_status.bat_in_recharging_state = true;
		BMT_status.bat_charging_state = CHR_BATFULL;
	}


	return PMU_STATUS_OK;
}


int BAT_BatteryHoldAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Hold mode !!\n\r");

	if (BMT_status.bat_vol < p_bat_charging_data->talking_recharge_voltage
	    || g_call_state == CALL_IDLE) {
		BMT_status.bat_charging_state = CHR_CC;
		battery_log(BAT_LOG_CRTI, "[BATTERY] Exit Hold mode and Enter CC mode !!\n\r");
	}

	bat_charger_enable(false);

	return PMU_STATUS_OK;
}

int BAT_BatteryCmdHoldAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Cmd Hold mode !!\n\r");

	if (!g_cmd_hold_charging) {
		BMT_status.bat_charging_state = CHR_CC;

		bat_charger_enable_power_path(true);
		battery_log(BAT_LOG_CRTI, "[BATTERY] Exit Cmd Hold mode and Enter CC mode !!\n\r");
	}

	BMT_status.bat_full = false;
	BMT_status.bat_in_recharging_state = false;
	BMT_status.total_charging_time = 0;
	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.POSTFULL_charging_time = 0;

	/*  Disable charger */
	bat_charger_enable(false);
	bat_charger_enable_power_path(false);

	return PMU_STATUS_OK;
}

int BAT_BatteryStatusFailAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] BAD Battery status... Charging Stop !!\n\r");

#if defined(CONFIG_MTK_JEITA_STANDARD_SUPPORT)
	if ((g_temp_status == TEMP_ABOVE_POS_60) || (g_temp_status == TEMP_BELOW_NEG_10)
	    || (g_temp_status == TEMP_NEG_10_TO_POS_0))
		temp_error_recovery_chr_flag = false;

	if ((temp_error_recovery_chr_flag == false) && (g_temp_status != TEMP_ABOVE_POS_60)
	    && (g_temp_status != TEMP_NEG_10_TO_POS_0)
	    && (g_temp_status != TEMP_BELOW_NEG_10)) {

		temp_error_recovery_chr_flag = true;
		BMT_status.bat_charging_state = CHR_PRE;
	}
#endif

	BMT_status.total_charging_time = 0;
	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.POSTFULL_charging_time = 0;

	/*  Disable charger */
	bat_charger_enable(false);

	return PMU_STATUS_OK;
}


void mt_battery_charging_algorithm(void)
{
	bat_charger_reset_watchdog_timer();

	switch (BMT_status.bat_charging_state) {
	case CHR_PRE:
		BAT_PreChargeModeAction();
		break;

	case CHR_CC:
		BAT_ConstantCurrentModeAction();
		break;

	case CHR_BATFULL:
		BAT_BatteryFullAction();
		break;

	case CHR_HOLD:
		BAT_BatteryHoldAction();
		break;

	case CHR_CMD_HOLD:
		BAT_BatteryCmdHoldAction();
		break;

	case CHR_ERROR:
		BAT_BatteryStatusFailAction();
		break;
	}

	bat_charger_dump_register();
}
