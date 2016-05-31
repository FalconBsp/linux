/*
 * Copyright 2014 Sony Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>

#include "hifc.h"
#include "hifc_drvif.h"

#define I2C_RD_DAT_DUM_NUM                 1
#define I2C_RD_DAT_HEAD_LEN                HEAD_PLACE_HOLDER
#define I2C_RD_DAT_LEN_TRIM                (HEAD_PLACE_HOLDER \
					   + I2C_RD_DAT_DUM_NUM)

#define I2C_WT_TAIL_DUM_NUM                1 /* I2C data tail dummy num */

/* WT 1: ICMD */
#define I2C_WT_1_POS_MSG_HEAD_ICMD         0
/* WT 1 pos in HEAD_PLACE_HOLDER */
#define I2C_WT_1_POS_MSG                   (HEAD_PLACE_HOLDER \
					   - I2C_WT_1_POS_MSG_HEAD_ICMD - 1)
#define I2C_WT_1_POS_ICMD                  (I2C_WT_1_POS_MSG \
					   + I2C_WT_1_POS_MSG_HEAD_ICMD)

/* WT 2: ICMD/len_low/len_high_flg */
#define I2C_WT_2_POS_MSG_HEAD_ICMD         0
#define I2C_WT_2_POS_MSG_HEAD_LEN_LOW      (I2C_WT_2_POS_MSG_HEAD_ICMD + 1)
#define I2C_WT_2_POS_MSG_HEAD_FLG_LEN_HIGH (I2C_WT_2_POS_MSG_HEAD_LEN_LOW + 1)
/* WT 2 pos in HEAD_PLACE_HOLDER */
#define I2C_WT_2_POS_MSG                   (HEAD_PLACE_HOLDER - \
					   I2C_WT_2_POS_MSG_HEAD_FLG_LEN_HIGH \
					   - 1)
#define I2C_WT_2_POS_ICMD                  (I2C_WT_2_POS_MSG \
					   + I2C_WT_2_POS_MSG_HEAD_ICMD)
#define I2C_WT_2_POS_LEN_LOW               (I2C_WT_2_POS_MSG \
					   + I2C_WT_2_POS_MSG_HEAD_LEN_LOW)
#define I2C_WT_2_POS_FLG_LEN_HIGH          (I2C_WT_2_POS_MSG \
					   + I2C_WT_2_POS_MSG_HEAD_FLG_LEN_HIGH)

/* WT 3: ICMD/addroffset/len_low/len_high_flg */
#define I2C_WT_3_POS_MSG_HEAD_ICMD         0
#define I2C_WT_3_POS_MSG_HEAD_OFST         (I2C_WT_3_POS_MSG_HEAD_ICMD + 1)
#define I2C_WT_3_POS_MSG_HEAD_LEN_LOW      (I2C_WT_3_POS_MSG_HEAD_OFST + 1)
#define I2C_WT_3_POS_MSG_HEAD_FLG_LEN_HIGH (I2C_WT_3_POS_MSG_HEAD_LEN_LOW + 1)
/* WT 3 pos in HEAD_PLACE_HOLDER */
#define I2C_WT_3_POS_MSG                   (HEAD_PLACE_HOLDER \
					   - I2C_WT_3_POS_MSG_HEAD_FLG_LEN_HIGH\
					   - 1)
#define I2C_WT_3_POS_ICMD                  (I2C_WT_3_POS_MSG \
					   + I2C_WT_3_POS_MSG_HEAD_ICMD)
#define I2C_WT_3_POS_OFST                  (I2C_WT_3_POS_MSG \
					   + I2C_WT_3_POS_MSG_HEAD_OFST)
#define I2C_WT_3_POS_LEN_LOW               (I2C_WT_3_POS_MSG \
					   + I2C_WT_3_POS_MSG_HEAD_LEN_LOW)
#define I2C_WT_3_POS_FLG_LEN_HIGH          (I2C_WT_3_POS_MSG \
					   + I2C_WT_3_POS_MSG_HEAD_FLG_LEN_HIGH)

#define I2C_WT_HEAD_WRAP_LEN               (HEAD_PLACE_HOLDER \
					   + I2C_WT_TAIL_DUM_NUM)

#define MAX_I2C_TRANS_LEN                  (64 * 1024)

static int32_t i2c_writef1_icmd(struct i2c_client *client,
				const uint8_t icmd, uint8_t *buf,
				const uint16_t len)
{
	uint16_t t_buf_len     = len + I2C_WT_HEAD_WRAP_LEN - I2C_WT_1_POS_MSG;
	uint8_t *t_buf         = buf + I2C_WT_1_POS_MSG;
	buf[I2C_WT_1_POS_ICMD] = icmd;

	dump_buffer(t_buf, t_buf_len);

	return i2c_master_send(client, t_buf,
			       MIN(t_buf_len, MAX_I2C_TRANS_LEN));
}

static int32_t i2c_writef2_icmd(struct i2c_client *client,
				const uint8_t icmd, uint8_t *buf,
				const uint16_t len, const uint8_t flg)
{
	uint16_t t_buf_len = len + I2C_WT_HEAD_WRAP_LEN - I2C_WT_2_POS_MSG;
	uint8_t *t_buf     = buf + I2C_WT_2_POS_MSG;

	buf[I2C_WT_2_POS_ICMD]         = icmd;
	buf[I2C_WT_2_POS_LEN_LOW]      = get_len_low_byte(len);
	buf[I2C_WT_2_POS_FLG_LEN_HIGH] = get_flg_len_high_byte(len, flg);

	return i2c_master_send(client, t_buf,
			       MIN(t_buf_len, MAX_I2C_TRANS_LEN));
}

static int32_t i2c_writef3_icmd(struct i2c_client *client,
				const uint8_t icmd, uint8_t *buf,
				const uint16_t len, const uint8_t ofst,
				const uint8_t flg)
{
	uint16_t t_buf_len = len + I2C_WT_HEAD_WRAP_LEN - I2C_WT_3_POS_MSG;
	uint8_t *t_buf     = buf + I2C_WT_3_POS_MSG;

	buf[I2C_WT_3_POS_ICMD]         = icmd;
	buf[I2C_WT_3_POS_OFST]         = ofst;
	buf[I2C_WT_3_POS_LEN_LOW]      = get_len_low_byte(len);
	buf[I2C_WT_3_POS_FLG_LEN_HIGH] = get_flg_len_high_byte(len, flg);

	dump_buffer(t_buf, t_buf_len);

	return i2c_master_send(client, t_buf,
			       MIN(t_buf_len, MAX_I2C_TRANS_LEN));
}

static int32_t i2c_send_icmd(void *client, const uint8_t icmd,
			     uint8_t *buf, const size_t len,
			     const uint8_t ofst, const uint8_t flg)
{
	int32_t ret = -EIO;

	switch (get_cmd_fmt(icmd)) {
	case CMD_FMT_WT_1:
	case CMD_FMT_RW_1:
		ret = i2c_writef1_icmd(client, icmd, buf, len);
		break;
	case CMD_FMT_WT_2:
	case CMD_FMT_RW_2:
		ret = i2c_writef2_icmd(client, icmd, buf, len, flg);
		break;
	case CMD_FMT_WT_3:
	case CMD_FMT_RW_3:
		ret = i2c_writef3_icmd(client, icmd, buf, len, ofst, flg);
		break;
	case CMD_FMT_MAX:
	default:
		HIFC_ERR("%s unsupported ICMD 0x%02x\n",
		       __func__, icmd);
		break;
	}

	/* HIFC_ALERT("%s icmd=0x%x, buf=%s, len=%d, ofst=%d, flg=%d\n",
		      __func__,
		      icmd, buf + HEAD_PLACE_HOLDER, len, ofst, flg); */
	return ret;
}

static int32_t i2c_readf1_icmd(struct i2c_client *client,
			       const uint8_t icmd, uint8_t *buf,
			       const uint16_t len)
{
	/* TODO, buffer alignment */
	int32_t ret        = 0;
	uint16_t t_buf_len = len + I2C_RD_DAT_DUM_NUM;
	uint16_t cmd_len = sizeof(icmd);

	ret = i2c_master_send(client, &icmd, cmd_len);

	if (cmd_len == ret) {
		ret = i2c_master_recv(client, buf + I2C_RD_DAT_HEAD_LEN,
				      MIN(t_buf_len, MAX_I2C_TRANS_LEN));
		/* TODO handle StatusFlg */
	}

	dump_buffer(buf + I2C_RD_DAT_LEN_TRIM, len);

	return ret;
}

static int32_t i2c_readf2_icmd(struct i2c_client *client,
			       const uint8_t icmd, uint8_t *buf,
			       const uint16_t len, const uint8_t flg)
{
	/* TODO, buffer alignment */
	int32_t ret        = 0;
	uint16_t t_buf_len = len + I2C_RD_DAT_DUM_NUM;
	uint8_t command[]  = {
		icmd,
		get_len_low_byte(len),
		get_flg_len_high_byte(len, flg)
	};
	uint16_t cmd_len = MIN(sizeof(command), MAX_I2C_TRANS_LEN);

	ret = i2c_master_send(client, command, cmd_len);

	if (cmd_len == ret) {
		ret = i2c_master_recv(client, buf + I2C_RD_DAT_HEAD_LEN,
				      MIN(t_buf_len, MAX_I2C_TRANS_LEN));
		/* TODO handle StatusFlg */
	}

	dump_buffer(buf + I2C_RD_DAT_LEN_TRIM, len);

	return ret;
}

static int32_t i2c_readf3_icmd(struct i2c_client *client,
			       const uint8_t icmd, uint8_t *buf,
			       const uint16_t len, const uint8_t ofst,
			       const uint8_t flg)
{
	/* TODO, buffer alignment */
	int32_t ret        = 0;
	uint16_t t_buf_len = len + I2C_RD_DAT_DUM_NUM;
	uint8_t command[]  = {
		icmd,
		ofst,
		get_len_low_byte(len),
		get_flg_len_high_byte(len, flg)
	};
	uint16_t cmd_len = MIN(sizeof(command), MAX_I2C_TRANS_LEN);

	ret = i2c_master_send(client, command, cmd_len);

	if (cmd_len == ret) {
		ret = i2c_master_recv(client, buf + I2C_RD_DAT_HEAD_LEN,
				      MIN(t_buf_len, MAX_I2C_TRANS_LEN));
		/* TODO handle StatusFlg */
	}

	dump_buffer(buf + I2C_RD_DAT_LEN_TRIM, len);

	return ret;
}

static int32_t i2c_recv_icmd(void *client, const uint8_t icmd,
			     uint8_t *buf, const size_t len,
			     const uint8_t ofst, const uint8_t flg,
			     uint8_t *buf_ofst)
{
	int32_t ret = -EIO;

	switch (get_cmd_fmt(icmd)) {
	case CMD_FMT_RD_0:
	case CMD_FMT_RD_1:
	case CMD_FMT_RW_1:
		ret = i2c_readf1_icmd(client, icmd, buf, len);
		break;
	case CMD_FMT_RD_2:
	case CMD_FMT_RW_2:
		ret = i2c_readf2_icmd(client, icmd, buf, len, flg);
		break;
	case CMD_FMT_RD_3:
	case CMD_FMT_RW_3:
		ret = i2c_readf3_icmd(client, icmd, buf, len, ofst, flg);
		break;
	case CMD_FMT_MAX:
	default:
		HIFC_ERR("%s unsupported ICMD 0x%02x\n",
		       __func__, icmd);
		break;
	}

	*buf_ofst = I2C_RD_DAT_LEN_TRIM;

	return ret;
}

static void i2c_enable_irq(const void *client)
{
	WARN_ON(!client);
	enable_irq(((struct i2c_client *)client)->irq);
}

static const struct hifc_bus_ops spz_buf_ops = {
	.spz_drv_send_icmd  = i2c_send_icmd,
	.spz_drv_recv_icmd  = i2c_recv_icmd,
	.spz_drv_enable_irq = i2c_enable_irq
};

static struct hifc_bus_data spz_bus_data = {
	.bops = &spz_buf_ops
};

static irqreturn_t hifc_isr(int32_t irq, void *dev_id)
{
	struct hifc_data *hdata = dev_id;
	struct i2c_client *client = hdata->bdata->client;

	disable_irq_nosync(client->irq);
	schedule_work(&hdata->work);

	return IRQ_HANDLED;
}

static int32_t init_client(struct i2c_client *client)
{
	return request_irq(client->irq,
			   hifc_isr,
			   IRQF_TRIGGER_HIGH,
			   "hifc_i2c",
			   dev_get_drvdata(&client->dev));
}

static int32_t spz_drv_i2c_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int32_t ret = 0;
	uint32_t trigger_io;

#ifdef CONFIG_OF
	if (client->dev.of_node != NULL) {
		if (of_property_read_u32(client->dev.of_node,
					 "hifc-trigger-io",
					 &trigger_io) != 0) {

			HIFC_ERR("no such property \"hifc-trigger-io\" in device node\n");

			return -ENODEV;
		}

		client->irq = gpio_to_irq(trigger_io);
	}
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		HIFC_ERR("%s: i2c funcs are not supported\n",
		       __func__);
		return -ENODEV;
	}

	HIFC_ALERT("%s, addr=0x%02x\n", __func__, client->addr);

	spz_bus_data.client = (void *)client;

	ret = spz_prot_probe(&client->dev, &spz_bus_data);

	return ret ? ret : init_client(client);
}

static int32_t deinit_client(struct i2c_client *client)
{
	free_irq(client->irq, dev_get_drvdata(&client->dev));

	return 0;
}

static int32_t spz_drv_i2c_remove(struct i2c_client *client)
{
	struct hifc_data *hdata =
			(struct hifc_data *)dev_get_drvdata(&client->dev);

	deinit_client(client);
	hdata->bdata = NULL;

	HIFC_ALERT("%s\n", __func__);
	return spz_prot_remove(&client->dev);
}

static const struct i2c_device_id spz_hifc_i2c_id[] = {
	{"hifc_i2c", 0},
	{}
};

static const struct of_device_id of_spz_hifc_match[] = {
	{.compatible = "hifc_i2c", },
	{}
};

static struct i2c_driver spz_hifc_i2c_driver = {
	.probe     = spz_drv_i2c_probe,
	.remove    = spz_drv_i2c_remove,
	.id_table  = spz_hifc_i2c_id,
	.driver    = {
		.name  = "hifc_i2c",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_spz_hifc_match),
	},
};

static int32_t __init spz_drv_i2c_init(void)
{
	HIFC_ALERT("%s\n", __func__);
	return i2c_add_driver(&spz_hifc_i2c_driver);
}

static void __exit spz_drv_i2c_exit(void)
{
	HIFC_ALERT("%s\n", __func__);
	return i2c_del_driver(&spz_hifc_i2c_driver);
}

module_init(spz_drv_i2c_init);
module_exit(spz_drv_i2c_exit);

/* MODULE_LICENSE("Proprietary"); */
MODULE_LICENSE("GPL");
