// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Paolo Sabatino <paolo.sabatino@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define FD6551_MAX_LEDS			8
#define FD6551_MAX_BRIGHTNESS	255
#define FD6551_MAX_DIGITS 		4

#define FD6551_DISPLAY_OFF		0
#define FD6551_DISPLAY_ON		BIT(0)

#define FD6551_STATUS_ADDR 	0x24
#define FD6551_ICONS_ADDR		0x33
#define FD6551_DIGITS_ADDR_BASE 0x34

#define ASCII_TABLE_OFFSET 		0x20
#define ASCII_MAX_CHAR_CODE (ARRAY_SIZE(ascii_to_seven) + ASCII_TABLE_OFFSET)

#define ldev_to_led(c)		container_of(c, struct fd6551_led, ldev)

#define SYSFS_CHARS chars
#define SYSFS_BRIGHTNESS brightness
#define SYSFS_MAX_BRIGHTNESS max_brightness

/*
 * Seven segment ascii conversion table.
 * Source: https://github.com/dmadison/LED-Segment-ASCII - (C) David Madison
 */
const char ascii_to_seven[96] = {
	0b00000000, /* (space) */
	0b10000110, /* ! */
	0b00100010, /* " */
	0b01111110, /* # */
	0b01101101, /* $ */
	0b11010010, /* % */
	0b01000110, /* & */
	0b00100000, /* ' */
	0b00101001, /* ( */
	0b00001011, /* ) */
	0b00100001, /* * */
	0b01110000, /* + */
	0b00010000, /* , */
	0b01000000, /* - */
	0b10000000, /* . */
	0b01010010, /* / */
	0b00111111, /* 0 */
	0b00000110, /* 1 */
	0b01011011, /* 2 */
	0b01001111, /* 3 */
	0b01100110, /* 4 */
	0b01101101, /* 5 */
	0b01111101, /* 6 */
	0b00000111, /* 7 */
	0b01111111, /* 8 */
	0b01101111, /* 9 */
	0b00001001, /* : */
	0b00001101, /* ; */
	0b01100001, /* < */
	0b01001000, /* = */
	0b01000011, /* > */
	0b11010011, /* ? */
	0b01011111, /* @ */
	0b01110111, /* A */
	0b01111100, /* B */
	0b00111001, /* C */
	0b01011110, /* D */
	0b01111001, /* E */
	0b01110001, /* F */
	0b00111101, /* G */
	0b01110110, /* H */
	0b00110000, /* I */
	0b00011110, /* J */
	0b01110101, /* K */
	0b00111000, /* L */
	0b00010101, /* M */
	0b00110111, /* N */
	0b00111111, /* O */
	0b01110011, /* P */
	0b01101011, /* Q */
	0b00110011, /* R */
	0b01101101, /* S */
	0b01111000, /* T */
	0b00111110, /* U */
	0b00111110, /* V */
	0b00101010, /* W */
	0b01110110, /* X */
	0b01101110, /* Y */
	0b01011011, /* Z */
	0b00111001, /* [ */
	0b01100100, /* \ */
	0b00001111, /* ] */
	0b00100011, /* ^ */
	0b00001000, /* _ */
	0b00000010, /* ` */
	0b01011111, /* a */
	0b01111100, /* b */
	0b01011000, /* c */
	0b01011110, /* d */
	0b01111011, /* e */
	0b01110001, /* f */
	0b01101111, /* g */
	0b01110100, /* h */
	0b00010000, /* i */
	0b00001100, /* j */
	0b01110101, /* k */
	0b00110000, /* l */
	0b00010100, /* m */
	0b01010100, /* n */
	0b01011100, /* o */
	0b01110011, /* p */
	0b01100111, /* q */
	0b01010000, /* r */
	0b01101101, /* s */
	0b01111000, /* t */
	0b00011100, /* u */
	0b00011100, /* v */
	0b00010100, /* w */
	0b01110110, /* x */
	0b01101110, /* y */
	0b01011011, /* z */
	0b01000110, /* { */
	0b00110000, /* | */
	0b01110000, /* } */
	0b00000001, /* ~ */
	0b00000000, /* (del) */
};

struct fd6551_led {
    unsigned int bit;
	struct led_classdev ldev;
	struct fd6551_priv *priv;
	struct i2c_client *i2c_client;
};

struct fd6551_priv {
	struct device *dev;
	const struct fd6551_chip *chip_data;
	struct fd6551_led leds[FD6551_MAX_LEDS];

	struct i2c_client *i2c_client_status;
	struct i2c_client *i2c_client_icon_set;
	struct i2c_client *i2c_client_digits[FD6551_MAX_DIGITS];

	/* follows led state data */
	unsigned int brightness;
	unsigned icon_set_bitmask;
	char text[FD6551_MAX_DIGITS];
};

struct fd6551_chip {
	unsigned int addr_status;
	unsigned int addr_icons;
	unsigned int addr_digits_base;
	bool has_icon_set;
	unsigned int digits;
	unsigned int brightness_levels;
};

static const struct fd6551_chip fd650 = {
	.addr_status = FD6551_STATUS_ADDR,
	.addr_digits_base = FD6551_DIGITS_ADDR_BASE,
	.has_icon_set = false,
	.digits = 4,
	.brightness_levels = 8
};

static const struct fd6551_chip fd6551 = {
	.addr_status = FD6551_STATUS_ADDR,
	.addr_icons = FD6551_ICONS_ADDR,
	.addr_digits_base = FD6551_DIGITS_ADDR_BASE,
	.has_icon_set = true,
	.digits = 4,
	.brightness_levels = 8
};

static int
fd6551_write(struct i2c_client *i2c_client, char value)
{
	return i2c_smbus_write_byte(i2c_client, value);
}

static int
fd6551_reset_leds(struct i2c_client *i2c_client)
{
	return fd6551_write(i2c_client, 0x0);
}

static int
fd6551_set_status(struct fd6551_priv *priv)
{

	int value;

	if (priv->brightness == 0)
		value = FD6551_DISPLAY_OFF;
	else
		value = FD6551_DISPLAY_ON | ((priv->chip_data->brightness_levels - priv->brightness) << 1);

	dev_dbg(priv->dev, "set status reg value: %02x\n", value);

	return fd6551_write(priv->i2c_client_status, value);

}

/*
 * Sysfs interface for reading and writing text on the display
 */
static ssize_t
chars_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fd6551_priv *priv = dev_get_drvdata(dev);
	strncpy(buf, (const char *)priv->text, FD6551_MAX_DIGITS);

	return strlen(priv->text);
}

static ssize_t
chars_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int idx, err;
	unsigned char ch, ch_bits;
	struct i2c_client *i2c_client;

	struct fd6551_priv *priv = dev_get_drvdata(dev);

	for (idx = 0; idx < priv->chip_data->digits; idx++) {

		if (idx < count)
			ch = buf[idx];
		else
			ch = ASCII_TABLE_OFFSET;

		priv->text[idx] = ch;

		ch = max(min(ch, ASCII_MAX_CHAR_CODE), ASCII_TABLE_OFFSET) - ASCII_TABLE_OFFSET;

		ch_bits = ascii_to_seven[(unsigned int)ch];

		i2c_client = priv->i2c_client_digits[idx];

		err = fd6551_write(i2c_client, ch_bits);
		if (err)
			dev_err(dev, "could not write character index %d, err: %d\n", idx, err);

	}

	return count;
}

static DEVICE_ATTR_RW(SYSFS_CHARS);

/*
 * Sysfs interface for read-only max_brightness property
 */
static ssize_t
max_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fd6551_priv *priv = dev_get_drvdata(dev);
	return snprintf(buf, 4, "%d", priv->chip_data->brightness_levels);
}

static DEVICE_ATTR_RO(SYSFS_MAX_BRIGHTNESS);

/*
 * Sysfs interface for reading and writing display brightness
 */
static ssize_t
brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fd6551_priv *priv = dev_get_drvdata(dev);
	return snprintf(buf, 4, "%d", priv->brightness);
}

static ssize_t
brightness_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int brightness;

	struct fd6551_priv *priv = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 0, &brightness);
	if (ret)
		return ret;

	priv->brightness = min(max(brightness, 0), priv->chip_data->brightness_levels);

	fd6551_set_status(priv);

	return count;
}

static DEVICE_ATTR_RW(SYSFS_BRIGHTNESS);

static int
fd6551_set_on(struct fd6551_priv *priv)
{
	priv->brightness = priv->chip_data->brightness_levels;
	return fd6551_set_status(priv);
}

static int
fd6551_set_off(struct fd6551_priv *priv)
{
	priv->brightness = 0;
	return fd6551_set_status(priv);

}

static int
fd6551_led_brightness_set(struct led_classdev *led_cdev,
                        enum led_brightness brightness)
{
	bool is_led_on;
	unsigned int mask, value;
	struct fd6551_led *led = ldev_to_led(led_cdev);
	struct fd6551_priv *priv = led->priv;

	is_led_on = brightness > 0;

	mask = BIT(led->bit);

	value = priv->icon_set_bitmask;
	value &= ~mask;
	value |= is_led_on ? mask : 0;

	priv->icon_set_bitmask = value;

	dev_dbg(priv->dev, "set led bit: 0x%02x, state: %s, mask: 0x%x, value: 0x%x\n", led->bit, is_led_on ? "on" : "off", mask, value);

	return fd6551_write(led->i2c_client, value);
}

static int
fd6551_init_leds(struct fd6551_priv *priv, struct device_node *np, const struct fd6551_chip *chip_data)
{

	struct device *dev;
	struct device_node *child;
	struct i2c_client *i2c_client_icon_set;
	int err;
	int count, idx = 0;

	dev = priv->dev;

	if (!priv->chip_data->has_icon_set)
		return 0;

	count = of_get_available_child_count(np);

	// No leds defined as child nodes in the device tree, skip the
	// whole i2c_client and leds registration
	if (count == 0)
		return 0;

	if (count > FD6551_MAX_LEDS) {
		dev_err(dev, "too many child nodes, driver can handle up to %d leds\n", FD6551_MAX_LEDS);
		return -EINVAL;
	}

	i2c_client_icon_set = i2c_new_ancillary_device(priv->i2c_client_status, "icon-set", chip_data->addr_icons);

	if (IS_ERR(i2c_client_icon_set)) {
		err = PTR_ERR(i2c_client_icon_set);
		dev_err(dev, "could not take ownership of icon set i2c address, err: %d\n", err);
		return err;
	}

	priv->i2c_client_icon_set = i2c_client_icon_set;

	fd6551_reset_leds(i2c_client_icon_set);

	for_each_available_child_of_node(np, child) {

		struct fd6551_led *led;
		struct led_init_data init_data = {};
		int bit;

		init_data.fwnode = of_fwnode_handle(child);

		err = of_property_read_u32(child, "bit", &bit);
		if (err) {
			dev_err(dev, "missing bit property for led %s\n", led->ldev.name);
			of_node_put(child);
			return err;
		}

		led = &priv->leds[idx];
		led->priv = priv;
		led->bit = bit;
		led->i2c_client = i2c_client_icon_set;
		led->ldev.brightness_set_blocking = fd6551_led_brightness_set;
		led->ldev.max_brightness = FD6551_MAX_BRIGHTNESS;
		err = devm_led_classdev_register_ext(dev, &led->ldev, &init_data);
		if (err < 0) {
			dev_err(dev, "couldn't register LED %s\n", led->ldev.name);
			of_node_put(child);
			return err;
		}

		dev_dbg(dev, "registered led %s\n", led->ldev.name);

		idx++;

	}

	return 0;

}

static int
fd6551_init_digits(struct fd6551_priv *priv, struct device_node *np, const struct fd6551_chip *chip_data)
{

	struct i2c_client *i2c_client_digit;
	struct device *dev;
	int idx, err, addr;
	char dig_name[8];
	bool reversed;

	dev = priv->dev;

	reversed = of_property_read_bool(np, "digits-reversed");

	for (idx = 1; idx <= priv->chip_data->digits; idx++) {

		snprintf(dig_name, sizeof(dig_name), "dig%d", idx);

		i2c_client_digit = i2c_new_ancillary_device(priv->i2c_client_status, dig_name, chip_data->addr_digits_base + idx - 1);
		if (IS_ERR(i2c_client_digit)) {
			err = PTR_ERR(i2c_client_digit);
			dev_err(dev, "could not register i2c client for digit %s, error: %d\n", dig_name, err);
			return err;
		}

		if (reversed)
			addr = priv->chip_data->digits - idx;
		else
			addr = idx - 1;

		priv->i2c_client_digits[addr] = i2c_client_digit;

		fd6551_reset_leds(i2c_client_digit);

		dev_dbg(dev, "registered i2c client for digit %s\n", dig_name);

	}

	return 0;

}

static void
fd6551_remove(struct i2c_client *i2c_client)
{

	struct i2c_client *i2c_client_digit;
	struct fd6551_priv *priv= i2c_get_clientdata(i2c_client);

	// Turn the display off
	if (priv->i2c_client_status)
		fd6551_set_off(priv);

	device_remove_file(priv->dev, &dev_attr_SYSFS_MAX_BRIGHTNESS);

	device_remove_file(priv->dev, &dev_attr_SYSFS_BRIGHTNESS);

	device_remove_file(priv->dev, &dev_attr_SYSFS_CHARS);

	if (priv->i2c_client_icon_set) {
		fd6551_reset_leds(priv->i2c_client_icon_set);
		i2c_unregister_device(priv->i2c_client_icon_set);
	}

	for (int idx = 0; idx < FD6551_MAX_DIGITS; idx++) {
		i2c_client_digit = priv->i2c_client_digits[idx];

		if (!i2c_client_digit)
			continue;

		fd6551_reset_leds(i2c_client_digit);
		i2c_unregister_device(i2c_client_digit);
	}

}

static void
fd6551_shutdown(struct i2c_client *i2c_client)
{

	fd6551_remove(i2c_client);

}

static int
fd6551_probe(struct i2c_client *i2c_client_status)
{

	struct device_node *np;
	struct device *dev = &i2c_client_status->dev;
	const struct fd6551_chip *chip_data;
	struct fd6551_priv *priv;
	int err;

	np = dev_of_node(dev);
	if (!np)
		return -ENODEV;

	chip_data = device_get_match_data(dev);
	if (!chip_data)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/*
	 * i2c_client_status is the first register (actually, address)
	 * of the chip; the chip uses several addresses for each group
	 * of leds, so we have to register an i2c_client for each of them
	 */
	priv->chip_data = chip_data;
	priv->i2c_client_status = i2c_client_status;
	priv->dev = dev;

	i2c_set_clientdata(i2c_client_status, priv);
	dev_set_drvdata(dev, priv);

	err = fd6551_init_leds(priv, np, chip_data);
	if (err)
		goto error_exit;

	err = fd6551_init_digits(priv, np, chip_data);
	if (err)
		goto error_exit;

	err = device_create_file(dev, &dev_attr_SYSFS_MAX_BRIGHTNESS);
	if (err) {
		dev_err(dev, "could not create sysfs interface for max_brightness property, err: %d\n", err);
		goto error_exit;
	}

	err = device_create_file(dev, &dev_attr_SYSFS_BRIGHTNESS);
	if (err) {
		dev_err(dev, "could not create sysfs interface for brightness control, err: %d\n", err);
		goto error_exit;
	}

	err = device_create_file(dev, &dev_attr_SYSFS_CHARS);
	if (err) {
		dev_err(dev, "could not create sysfs interface for character control, err: %d\n", err);
		goto error_exit;
	}

	err = fd6551_set_on(priv);
	if (err < 0)
		goto error_exit;

	return 0;

error_exit:

	// Do cleanup before exiting due to errors

	fd6551_remove(i2c_client_status);

	return err;

}

static const struct of_device_id of_fd6551_leds_match[] = {
	{ .compatible = "fdhisi,fd6551",
		.data = &fd6551 },
	{ .compatible = "fdhisi,fd650",
		.data = &fd650 },
	{ .compatible = "titanmicro,tm1650",
		.data = &fd650 },
	{ /* Sentinel */},
};
MODULE_DEVICE_TABLE(of, of_fd6551_leds_match);

static struct i2c_driver fd6551_driver = {
	.driver = {
		.name = "fd6551",
		.of_match_table = of_match_ptr(of_fd6551_leds_match),
	},
	.probe = fd6551_probe,
	.remove = fd6551_remove,
	.shutdown = fd6551_shutdown
};

module_i2c_driver(fd6551_driver);

MODULE_AUTHOR("Paolo Sabatino <paolo.sabatino@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FD6551 and compatibles LED driver");
