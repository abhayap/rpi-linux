/*
 * Cirrus Audio Card wm8804 preinit code
 *
 * Refactored out of the Cirrus Audio Card code to work around
 * init dependecy issues.
 *
 * Copyright 2015 Cirrus Logic Inc.
 *
 * Author:	Nikesh Oswal, <Nikesh.Oswal@wolfsonmicro.com>
 * Author:	Matthias Reichl, <hias@horus.com>
 * Partly based on sound/soc/bcm/iqaudio-dac.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/system_info.h>

/*TODO: Shift this to platform data*/
#define GPIO_WM8804_RST 8
#define GPIO_WM8804_MODE 2
#define GPIO_WM8804_SW_MODE 23
#define GPIO_WM8804_I2C_ADDR_B 18
#define GPIO_WM8804_I2C_ADDR_B_PLUS 13

static void bcm2708_set_gpio_alt(int pin, int alt)
{
	/*
	 * This is the common way to handle the GPIO pins for
	 * the Raspberry Pi.
	 * TODO This is a hack. Use pinmux / pinctrl.
	 */
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))
	unsigned int *gpio;
	gpio = ioremap(GPIO_BASE, SZ_16K);
	INP_GPIO(pin);
	SET_GPIO_ALT(pin, alt);
	iounmap(gpio);
#undef INP_GPIO
#undef SET_GPIO_ALT
}

static int wm8804_reset(void)
 {
	int ret;
	unsigned int gpio_wm8804_i2c_addr;

	if ((system_rev & 0xffffff) >= 0x10) {
		/* Model B+ or later */
		gpio_wm8804_i2c_addr = GPIO_WM8804_I2C_ADDR_B_PLUS;
	} else {
		gpio_wm8804_i2c_addr = GPIO_WM8804_I2C_ADDR_B;
	}

	if (!gpio_is_valid(GPIO_WM8804_RST)) {
		pr_err("Skipping unavailable gpio %d (%s)\n", GPIO_WM8804_RST, "wm8804_rst");
		return -ENOMEM;
	}

	if (!gpio_is_valid(GPIO_WM8804_MODE)) {
		pr_err("Skipping unavailable gpio %d (%s)\n", GPIO_WM8804_MODE, "wm8804_mode");
		return -ENOMEM;
	}

	if (!gpio_is_valid(GPIO_WM8804_SW_MODE)) {
		pr_err("Skipping unavailable gpio %d (%s)\n", GPIO_WM8804_SW_MODE, "wm8804_sw_mode");
		return -ENOMEM;
	}

	if (!gpio_is_valid(gpio_wm8804_i2c_addr)) {
		pr_err("Skipping unavailable gpio %d (%s)\n", gpio_wm8804_i2c_addr, "wm8804_i2c_addr");
		return -ENOMEM;
	}

	ret = gpio_request(GPIO_WM8804_RST, "wm8804_rst");
	if (ret < 0) {
		pr_err("gpio_request wm8804_rst failed\n");
		return ret;
	}

	ret = gpio_request(GPIO_WM8804_MODE, "wm8804_mode");
	if (ret < 0) {
		pr_err("gpio_request wm8804_mode failed\n");
		return ret;
	}

	ret = gpio_request(GPIO_WM8804_SW_MODE, "wm8804_sw_mode");
	if (ret < 0) {
		pr_err("gpio_request wm8804_sw_mode failed\n");
		return ret;
	}

	ret = gpio_request(gpio_wm8804_i2c_addr, "wm8804_i2c_addr");
	if (ret < 0) {
		pr_err("gpio_request wm8804_i2c_addr failed\n");
		return ret;
	}

	/*GPIO2 is used for SW/HW Mode Select and after Reset the same pin is used as
	I2C data line, so initially it is configured as GPIO OUT from BCM perspective*/
	/*Set SW Mode*/
	ret = gpio_direction_output(GPIO_WM8804_MODE, 1);
	if (ret < 0) {
		pr_err("gpio_direction_output wm8804_mode failed\n");
	}

	/*Set 2 Wire (I2C) Mode*/
	ret = gpio_direction_output(GPIO_WM8804_SW_MODE, 0);
	if (ret < 0) {
		pr_err("gpio_direction_output wm8804_sw_mode failed\n");
	}

	/*Set 2 Wire (I2C) Addr to 0x3A, writing 1 will make the Addr as 0x3B*/
	ret = gpio_direction_output(gpio_wm8804_i2c_addr, 0);
	if (ret < 0) {
		pr_err("gpio_direction_output wm8804_i2c_addr failed\n");
	}

	/*Take WM8804 out of reset*/
	ret = gpio_direction_output(GPIO_WM8804_RST, 1);
	if (ret < 0) {
		pr_err("gpio_direction_output wm8804_rst failed\n");
	}

	/*Put WM8804 in reset*/
	gpio_set_value(GPIO_WM8804_RST, 0);
	mdelay(500);
	/*Take WM8804 out of reset*/
	gpio_set_value(GPIO_WM8804_RST, 1);
	mdelay(500);

	gpio_free(GPIO_WM8804_RST);
	gpio_free(GPIO_WM8804_MODE);
	gpio_free(GPIO_WM8804_SW_MODE);
	gpio_free(gpio_wm8804_i2c_addr);

	/*GPIO2 is used for SW/HW Mode Select and after Reset the same pin is used as
	I2C data line, so after reset  it is configured as I2C data line i.e ALT0 function*/
	bcm2708_set_gpio_alt(GPIO_WM8804_MODE, 0);

	return ret;
}

static int snd_rpi_wsp_preinit_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	dev_info(dev, "initializing wm8804 on Cirrus audio card\n");
	ret = wm8804_reset();
	if (ret)
		dev_err(dev, "wm8804_reset returned %d\n", ret);

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id snd_rpi_wsp_preinit_of_match[] = {
		{ .compatible = "wlf,rpi-wm5102-preinit", },
		{},
};
MODULE_DEVICE_TABLE(of, snd_rpi_wsp_preinit_of_match);
#endif /* CONFIG_OF */

static struct platform_driver snd_rpi_wsp_preinit_driver = {
	.driver = {
		.name   = "snd-rpi-wsp-preinit",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(snd_rpi_wsp_preinit_of_match),
	},
	.probe	  = snd_rpi_wsp_preinit_probe,
};

module_platform_driver(snd_rpi_wsp_preinit_driver);

MODULE_AUTHOR("Nikesh Oswal");
MODULE_AUTHOR("Liu Xin");
MODULE_AUTHOR("Matthias Reichl");
MODULE_DESCRIPTION("Cirrus sound pi wm8804 one-time initialisation code");
MODULE_LICENSE("GPL");
