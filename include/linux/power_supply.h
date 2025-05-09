/*
 *  Universal power supply monitor class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 *
 *  You may use this code as per GPL version 2
 */

#ifndef __LINUX_POWER_SUPPLY_H__
#define __LINUX_POWER_SUPPLY_H__

#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/types.h>

/*
 * All voltages, currents, charges, energies, time and temperatures in uV,
 * µA, µAh, µWh, seconds and tenths of degree Celsius unless otherwise
 * stated. It's driver's job to convert its raw values to units in which
 * this class operates.
 */

/*
 * For systems where the charger determines the maximum battery capacity
 * the min and max fields should be used to present these values to user
 * space. Unused/unknown fields will not appear in sysfs.
 */

enum {
	POWER_SUPPLY_STATUS_UNKNOWN = 0,
	POWER_SUPPLY_STATUS_CHARGING,
	POWER_SUPPLY_STATUS_DISCHARGING,
	POWER_SUPPLY_STATUS_NOT_CHARGING,
	POWER_SUPPLY_STATUS_FULL,
};

enum {
	POWER_SUPPLY_CHARGE_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_CHARGE_TYPE_NONE,
	POWER_SUPPLY_CHARGE_TYPE_TRICKLE,
	POWER_SUPPLY_CHARGE_TYPE_FAST,
	POWER_SUPPLY_CHARGE_TYPE_SLOW,
};

enum {
	POWER_SUPPLY_HEALTH_UNKNOWN = 0,
	POWER_SUPPLY_HEALTH_GOOD,
	POWER_SUPPLY_HEALTH_OVERHEAT,
	POWER_SUPPLY_HEALTH_WARM,
	POWER_SUPPLY_HEALTH_DEAD,
	POWER_SUPPLY_HEALTH_OVERVOLTAGE,
	POWER_SUPPLY_HEALTH_UNSPEC_FAILURE,
	POWER_SUPPLY_HEALTH_COLD,
	POWER_SUPPLY_HEALTH_COOL,
	POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_UNDERVOLTAGE,
	POWER_SUPPLY_HEALTH_OVERHEATLIMIT,
	POWER_SUPPLY_HEALTH_MAX,
};

enum {
	POWER_SUPPLY_TECHNOLOGY_UNKNOWN = 0,
	POWER_SUPPLY_TECHNOLOGY_NiMH,
	POWER_SUPPLY_TECHNOLOGY_LION,
	POWER_SUPPLY_TECHNOLOGY_LIPO,
	POWER_SUPPLY_TECHNOLOGY_LiFe,
	POWER_SUPPLY_TECHNOLOGY_NiCd,
	POWER_SUPPLY_TECHNOLOGY_LiMn,
};

enum {
	POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN = 0,
	POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL,
	POWER_SUPPLY_CAPACITY_LEVEL_LOW,
	POWER_SUPPLY_CAPACITY_LEVEL_NORMAL,
	POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
	POWER_SUPPLY_CAPACITY_LEVEL_FULL,
};

enum {
	POWER_SUPPLY_SCOPE_UNKNOWN = 0,
	POWER_SUPPLY_SCOPE_SYSTEM,
	POWER_SUPPLY_SCOPE_DEVICE,
};

enum power_supply_property {
	/* Properties of type `int' */
	POWER_SUPPLY_PROP_STATUS = 0,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_AUTHENTIC,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_BOOT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_FULL,
	POWER_SUPPLY_PROP_CURRENT_BOOT,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_AVG,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_OTG_CONTROL,
	POWER_SUPPLY_PROP_CHARGE_POWERED_OTG_CONTROL,
	POWER_SUPPLY_PROP_CHARGE_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_COUNTER_SHADOW,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_EMPTY,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_AVG,
	POWER_SUPPLY_PROP_CAPACITY, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_MAX,
	POWER_SUPPLY_PROP_TEMP_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TYPE, /* use power_supply.type instead */
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CALIBRATE,
	/* Local extensions */
	POWER_SUPPLY_PROP_USB_HC,
	POWER_SUPPLY_PROP_USB_OTG,
	POWER_SUPPLY_PROP_CHARGE_ENABLED,
	POWER_SUPPLY_PROP_FUELGAUGE_RESET,
	POWER_SUPPLY_PROP_USBPD_RESET,
	POWER_SUPPLY_PROP_FACTORY_MODE,
	POWER_SUPPLY_PROP_SOH,
	POWER_SUPPLY_PROP_PD_SUPPORT,
	/* Local extensions of type int64_t */
	POWER_SUPPLY_PROP_CHARGE_COUNTER_EXT,
	/* Properties of type `const char *' */
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_AFC_CHARGER_MODE,
	POWER_SUPPLY_PROP_VCHGIN,
	POWER_SUPPLY_PROP_VWCIN,
	POWER_SUPPLY_PROP_VBYP,
	POWER_SUPPLY_PROP_VSYS,
	POWER_SUPPLY_PROP_VBAT,
	POWER_SUPPLY_PROP_VGPADC,
	POWER_SUPPLY_PROP_VCC1,
	POWER_SUPPLY_PROP_VCC2,
	POWER_SUPPLY_PROP_ICHGIN,
	POWER_SUPPLY_PROP_IWCIN,
	POWER_SUPPLY_PROP_IOTG,
	POWER_SUPPLY_PROP_ITX,
	POWER_SUPPLY_PROP_CO_ENABLE,
	POWER_SUPPLY_PROP_RR_ENABLE,
	POWER_SUPPLY_PROP_PM_FACTORY,
	POWER_SUPPLY_PROP_TDIE,
	POWER_SUPPLY_PROP_FAST_IAVG,
//	POWER_SUPPLY_PROP_USBPD_RESET,
//	POWER_SUPPLY_PROP_USBPD_TEST_READ,
	POWER_SUPPLY_PROP_MAX,
	POWER_SUPPLY_EXT_PROP_MAX = POWER_SUPPLY_PROP_MAX + 256,
	POWER_SUPPLY_LSI_PROP_MAX = POWER_SUPPLY_EXT_PROP_MAX + 256,
};

enum power_supply_type {
	POWER_SUPPLY_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_TYPE_BATTERY,			/* 1 */
	POWER_SUPPLY_TYPE_UPS,				/* 2 */
	POWER_SUPPLY_TYPE_MAINS,			/* 3 */
	POWER_SUPPLY_TYPE_USB,				/* Standard Downstream Port (4) */
	POWER_SUPPLY_TYPE_USB_DCP,			/* Dedicated Charging Port (5) */
	POWER_SUPPLY_TYPE_USB_CDP,			/* Charging Downstream Port (6) */
	POWER_SUPPLY_TYPE_USB_ACA,			/* Accessory Charger Adapters (7) */
	POWER_SUPPLY_TYPE_BMS,				/* Battery Monitor System (8) */
	POWER_SUPPLY_TYPE_MISC,				/* 9 */
	POWER_SUPPLY_TYPE_WIRELESS,			/* 10 */
	POWER_SUPPLY_TYPE_HV_WIRELESS,			/* 11 */
	POWER_SUPPLY_TYPE_PMA_WIRELESS, 		/* 12 */
	POWER_SUPPLY_TYPE_CARDOCK,			/* 13 */
	POWER_SUPPLY_TYPE_UARTOFF,			/* 14 */
	POWER_SUPPLY_TYPE_OTG,				/* 15 */
	POWER_SUPPLY_TYPE_LAN_HUB,			/* 16 */
	POWER_SUPPLY_TYPE_MHL_500,			/* 17 */
	POWER_SUPPLY_TYPE_MHL_900,			/* 18 */
	POWER_SUPPLY_TYPE_MHL_1500, 			/* 19 */
	POWER_SUPPLY_TYPE_MHL_USB,			/* 20 */
	POWER_SUPPLY_TYPE_SMART_OTG,			/* 21 */
	POWER_SUPPLY_TYPE_SMART_NOTG,			/* 22 */
	POWER_SUPPLY_TYPE_POWER_SHARING,		/* power sharing cable(23) */
	POWER_SUPPLY_TYPE_HV_MAINS, 			/* 24 */
	POWER_SUPPLY_TYPE_HV_MAINS_12V,			/* 25 */
	POWER_SUPPLY_TYPE_HV_PREPARE_MAINS, 		/* 26 */
	POWER_SUPPLY_TYPE_HV_ERR,			/* 27 */
	POWER_SUPPLY_TYPE_MHL_USB_100,			/* 28 */
	POWER_SUPPLY_TYPE_MHL_2000, 			/* 29 */
	POWER_SUPPLY_TYPE_HV_UNKNOWN,			/* 30 */
	POWER_SUPPLY_TYPE_MDOCK_TA, 			/* Mdock charger(31) */
	POWER_SUPPLY_TYPE_HMT_CONNECTED,		/* 32 */
	POWER_SUPPLY_TYPE_HMT_CHARGE,			/* 33 */
	POWER_SUPPLY_TYPE_WIRELESS_PACK,		/* 34 */
	POWER_SUPPLY_TYPE_WIRELESS_PACK_TA,		/* 35 */
	POWER_SUPPLY_TYPE_WIRELESS_STAND,       /* 36 */
	POWER_SUPPLY_TYPE_WIRELESS_HV_STAND,    /* 37 */
	POWER_SUPPLY_TYPE_PDIC,             	/* 38 */
	POWER_SUPPLY_TYPE_HV_MAINS_CHG_LIMIT,	/* 39 */
	POWER_SUPPLY_TYPE_HV_QC20,			/* 40 - ILIM 1.8A - CHG 3.0A */
	POWER_SUPPLY_TYPE_HV_QC30,			/* 41 - ILIM 3.0A - CHG 3.5A (Step Charging 4.2A -> 3.0A) */
	POWER_SUPPLY_TYPE_POGO,			/* POGO */
	POWER_SUPPLY_TYPE_MAX,
};

enum power_supply_notifier_events {
	PSY_EVENT_PROP_CHANGED,
	PSY_EVENT_PROP_ADDED,
};

union power_supply_propval {
	int intval;
	const char *strval;
	int64_t int64val;
};

struct device_node;
struct power_supply;

/* Run-time specific power supply configuration */
struct power_supply_config {
	struct device_node *of_node;
	/* Driver private data */
	void *drv_data;

	char **supplied_to;
	size_t num_supplicants;
};

/* Description of power supply */
struct power_supply_desc {
	const char *name;
	enum power_supply_type type;
	enum power_supply_property *properties;
	size_t num_properties;

	/*
	 * Functions for drivers implementing power supply class.
	 * These shouldn't be called directly by other drivers for accessing
	 * this power supply. Instead use power_supply_*() functions (for
	 * example power_supply_get_property()).
	 */
	int (*get_property)(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val);
	int (*set_property)(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val);
	/*
	 * property_is_writeable() will be called during registration
	 * of power supply. If this happens during device probe then it must
	 * not access internal data of device (because probe did not end).
	 */
	int (*property_is_writeable)(struct power_supply *psy,
				     enum power_supply_property psp);
	void (*external_power_changed)(struct power_supply *psy);
	void (*set_charged)(struct power_supply *psy);

	/*
	 * Set if thermal zone should not be created for this power supply.
	 * For example for virtual supplies forwarding calls to actual
	 * sensors or other supplies.
	 */
	bool no_thermal;
	/* For APM emulation, think legacy userspace. */
	int use_for_apm;
};

struct power_supply {
	const struct power_supply_desc *desc;

	char **supplied_to;
	size_t num_supplicants;

	char **supplied_from;
	size_t num_supplies;
	struct device_node *of_node;

	/* Driver private data */
	void *drv_data;

	/* private */
	struct device dev;
	struct work_struct changed_work;
	struct delayed_work deferred_register_work;
	spinlock_t changed_lock;
	bool changed;
	bool initialized;
	bool removing;
	atomic_t use_cnt;
#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzd;
	struct thermal_cooling_device *tcd;
#endif

#ifdef CONFIG_LEDS_TRIGGERS
	struct led_trigger *charging_full_trig;
	char *charging_full_trig_name;
	struct led_trigger *charging_trig;
	char *charging_trig_name;
	struct led_trigger *full_trig;
	char *full_trig_name;
	struct led_trigger *online_trig;
	char *online_trig_name;
	struct led_trigger *charging_blink_full_solid_trig;
	char *charging_blink_full_solid_trig_name;
#endif
};

/*
 * This is recommended structure to specify static power supply parameters.
 * Generic one, parametrizable for different power supplies. Power supply
 * class itself does not use it, but that's what implementing most platform
 * drivers, should try reuse for consistency.
 */

struct power_supply_info {
	const char *name;
	int technology;
	int voltage_max_design;
	int voltage_min_design;
	int charge_full_design;
	int charge_empty_design;
	int energy_full_design;
	int energy_empty_design;
	int use_for_apm;
};

/*
 * This is the recommended struct to manage static battery parameters,
 * populated by power_supply_get_battery_info(). Most platform drivers should
 * use these for consistency.
 * Its field names must correspond to elements in enum power_supply_property.
 * The default field value is -EINVAL.
 * Power supply class itself doesn't use this.
 */

struct power_supply_battery_info {
	int energy_full_design_uwh;	    /* microWatt-hours */
	int charge_full_design_uah;	    /* microAmp-hours */
	int voltage_min_design_uv;	    /* microVolts */
	int precharge_current_ua;	    /* microAmps */
	int charge_term_current_ua;	    /* microAmps */
	int constant_charge_current_max_ua; /* microAmps */
	int constant_charge_voltage_max_uv; /* microVolts */
};

extern struct atomic_notifier_head power_supply_notifier;
extern int power_supply_reg_notifier(struct notifier_block *nb);
extern void power_supply_unreg_notifier(struct notifier_block *nb);
extern struct power_supply *power_supply_get_by_name(const char *name);
extern void power_supply_put(struct power_supply *psy);
#ifdef CONFIG_OF
extern struct power_supply *power_supply_get_by_phandle(struct device_node *np,
							const char *property);
extern struct power_supply *devm_power_supply_get_by_phandle(
				    struct device *dev, const char *property);
#else /* !CONFIG_OF */
static inline struct power_supply *
power_supply_get_by_phandle(struct device_node *np, const char *property)
{ return NULL; }
static inline struct power_supply *
devm_power_supply_get_by_phandle(struct device *dev, const char *property)
{ return NULL; }
#endif /* CONFIG_OF */

extern int power_supply_get_battery_info(struct power_supply *psy,
					 struct power_supply_battery_info *info);
extern void power_supply_changed(struct power_supply *psy);
extern int power_supply_am_i_supplied(struct power_supply *psy);
int power_supply_get_property_from_supplier(struct power_supply *psy,
					    enum power_supply_property psp,
					    union power_supply_propval *val);
extern int power_supply_set_battery_charged(struct power_supply *psy);

#ifdef CONFIG_POWER_SUPPLY
extern int power_supply_is_system_supplied(void);
#else
static inline int power_supply_is_system_supplied(void) { return -ENOSYS; }
#endif

extern int power_supply_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val);
extern int power_supply_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val);
extern int power_supply_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp);
extern void power_supply_external_power_changed(struct power_supply *psy);

extern struct power_supply *__must_check
power_supply_register(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct power_supply *__must_check
power_supply_register_no_ws(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct power_supply *__must_check
devm_power_supply_register(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct power_supply *__must_check
devm_power_supply_register_no_ws(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern void power_supply_unregister(struct power_supply *psy);
extern int power_supply_powers(struct power_supply *psy, struct device *dev);

extern void *power_supply_get_drvdata(struct power_supply *psy);
/* For APM emulation, think legacy userspace. */
extern struct class *power_supply_class;

static inline bool power_supply_is_amp_property(enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_CHARGE_AVG:
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_CURRENT_BOOT:
		return 1;
	default:
		break;
	}

	return 0;
}

static inline bool power_supply_is_watt_property(enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_ENERGY_EMPTY:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_ENERGY_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
	case POWER_SUPPLY_PROP_VOLTAGE_BOOT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_POWER_NOW:
		return 1;
	default:
		break;
	}

	return 0;
}

#endif /* __LINUX_POWER_SUPPLY_H__ */
