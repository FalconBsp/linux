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
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#endif

#include "hifc.h"
#include "hifc_drvif.h"

/* spi rd icmd data tail dummy num */
#define SPI_RD_TAIL_DUM_NUM                2
/* spi wt icmd data tail dummy num */
#define SPI_WT_TAIL_DUM_NUM                2
#define SPI_RD_MSG_HEAD_DUM_NUM            HEAD_DUM_NUM_RD
#define SPI_WT_MSG_HEAD_DUM_NUM            HEAD_DUM_NUM_WT

/* RD 1: ICMD/dummy.. */
#define SPI_RD_1_POS_MSG_HEAD_ICMD         0
/* RD 1 pos in HEAD_PLACE_HOLDER */
#define SPI_RD_1_POS_MSG                   (HEAD_PLACE_HOLDER \
					    - SPI_RD_1_POS_MSG_HEAD_ICMD \
					    - SPI_RD_MSG_HEAD_DUM_NUM - 1)
#define SPI_RD_1_POS_ICMD                  (SPI_RD_1_POS_MSG \
					    + SPI_RD_1_POS_MSG_HEAD_ICMD)
/* RD 2: ICMD/LEN_LOW/dummy../FLG_LEN_HIGH */
#define SPI_RD_2_POS_MSG_HEAD_ICMD         0
#define SPI_RD_2_POS_MSG_HEAD_LEN_LOW      (SPI_RD_2_POS_MSG_HEAD_ICMD + 1)
#define SPI_RD_2_POS_MSG_HEAD_FLG_LEN_HIGH (SPI_RD_2_POS_MSG_HEAD_LEN_LOW \
					    + SPI_RD_MSG_HEAD_DUM_NUM + 1)
/* RD 2 pos in HEAD_PLACE_HOLDER */
#define SPI_RD_2_POS_MSG                   (HEAD_PLACE_HOLDER -\
					    SPI_RD_2_POS_MSG_HEAD_FLG_LEN_HIGH\
					    - 1)
#define SPI_RD_2_POS_ICMD                  (SPI_RD_2_POS_MSG \
					    + SPI_RD_2_POS_MSG_HEAD_ICMD)
#define SPI_RD_2_POS_LEN_LOW               (SPI_RD_2_POS_MSG \
					    + SPI_RD_2_POS_MSG_HEAD_LEN_LOW)
#define SPI_RD_2_POS_FLG_LEN_HIGH          (SPI_RD_2_POS_MSG +\
					    SPI_RD_2_POS_MSG_HEAD_FLG_LEN_HIGH)

/* RD 3: ICMD/addroffset/dummy../LEN_LOW/FLG_LEN_HIGH */
#define SPI_RD_3_POS_MSG_HEAD_ICMD         0
#define SPI_RD_3_POS_MSG_HEAD_OFST         (SPI_RD_3_POS_MSG_HEAD_ICMD + 1)
#define SPI_RD_3_POS_MSG_HEAD_LEN_LOW      (SPI_RD_3_POS_MSG_HEAD_OFST \
					    + SPI_RD_MSG_HEAD_DUM_NUM + 1)
#define SPI_RD_3_POS_MSG_HEAD_FLG_LEN_HIGH (SPI_RD_3_POS_MSG_HEAD_LEN_LOW + 1)

/* RD 3 in HEAD_PLACE_HOLDER */
#define SPI_RD_3_POS_MSG                   (HEAD_PLACE_HOLDER -\
					    SPI_RD_3_POS_MSG_HEAD_FLG_LEN_HIGH\
					    - 1)
#define SPI_RD_3_POS_ICMD                  (SPI_RD_3_POS_MSG \
					    + SPI_RD_3_POS_MSG_HEAD_ICMD)
#define SPI_RD_3_POS_OFST                  (SPI_RD_3_POS_MSG \
					    + SPI_RD_3_POS_MSG_HEAD_OFST)
#define SPI_RD_3_POS_LEN_LOW               (SPI_RD_3_POS_MSG \
					    + SPI_RD_3_POS_MSG_HEAD_LEN_LOW)
#define SPI_RD_3_POS_FLG_LEN_HIGH          (SPI_RD_3_POS_MSG +\
					    SPI_RD_3_POS_MSG_HEAD_FLG_LEN_HIGH)

/* WT 1: ICMD */
#define SPI_WT_1_POS_MSG_HEAD_ICMD         0
/* WT 1 pos in HEAD_PLACE_HOLDER */
#define SPI_WT_1_POS_MSG                   (HEAD_PLACE_HOLDER \
					    - SPI_WT_MSG_HEAD_DUM_NUM - 1)
#define SPI_WT_1_POS_ICMD                  (SPI_WT_1_POS_MSG \
					    + SPI_WT_1_POS_MSG_HEAD_ICMD)

/* WT 2: ICMD/LEN_LOW/FLG_LEN_HIGH */
#define SPI_WT_2_POS_MSG_HEAD_ICMD         0
#define SPI_WT_2_POS_MSG_HEAD_LEN_LOW      (SPI_WT_2_POS_MSG_HEAD_ICMD + 1)
#define SPI_WT_2_POS_MSG_HEAD_FLG_LEN_HIGH (SPI_WT_2_POS_MSG_HEAD_LEN_LOW + 1)

/* WT 2 pos in HEAD_PLACE_HOLDER */
#define SPI_WT_2_POS_MSG                   (HEAD_PLACE_HOLDER -\
					    SPI_WT_2_POS_MSG_HEAD_FLG_LEN_HIGH\
					    - 1)
#define SPI_WT_2_POS_ICMD                  (SPI_WT_2_POS_MSG \
					    + SPI_WT_2_POS_MSG_HEAD_ICMD)
#define SPI_WT_2_POS_LEN_LOW               (SPI_WT_2_POS_MSG \
					    + SPI_WT_2_POS_MSG_HEAD_LEN_LOW)
#define SPI_WT_2_POS_FLG_LEN_HIGH          (SPI_WT_2_POS_MSG + \
					    SPI_WT_2_POS_MSG_HEAD_FLG_LEN_HIGH)

/* WT 3: ICMD/addroffset/LEN_LOW/FLG_LEN_HIGH */
#define SPI_WT_3_POS_MSG_HEAD_ICMD         0
#define SPI_WT_3_POS_MSG_HEAD_OFST         (SPI_WT_3_POS_MSG_HEAD_ICMD + 1)
#define SPI_WT_3_POS_MSG_HEAD_LEN_LOW      (SPI_WT_3_POS_MSG_HEAD_OFST + 1)
#define SPI_WT_3_POS_MSG_HEAD_FLG_LEN_HIGH (SPI_WT_3_POS_MSG_HEAD_LEN_LOW + 1)

/* WT 3 pos in HEAD_PLACE_HOLDER */
#define SPI_WT_3_POS_MSG                   (HEAD_PLACE_HOLDER - \
					    SPI_WT_3_POS_MSG_HEAD_FLG_LEN_HIGH\
					    - 1)
#define SPI_WT_3_POS_ICMD                  (SPI_WT_3_POS_MSG \
					    + SPI_WT_3_POS_MSG_HEAD_ICMD)
#define SPI_WT_3_POS_OFST                  (SPI_WT_3_POS_MSG \
					    + SPI_WT_3_POS_MSG_HEAD_OFST)
#define SPI_WT_3_POS_LEN_LOW               (SPI_WT_3_POS_MSG \
					    + SPI_WT_3_POS_MSG_HEAD_LEN_LOW)
#define SPI_WT_3_POS_FLG_LEN_HIGH          (SPI_WT_3_POS_MSG + \
					    SPI_WT_3_POS_MSG_HEAD_FLG_LEN_HIGH)

#define SPI_RD_HEAD_WRAP_LEN               (HEAD_PLACE_HOLDER \
					    + SPI_RD_TAIL_DUM_NUM)
#define SPI_WT_HEAD_WRAP_LEN               (HEAD_PLACE_HOLDER \
					    + SPI_WT_TAIL_DUM_NUM)

#define SPI_RD_DAT_DUM_TRIM                3
#define SPI_RD_1_DAT_LEN_TRIM              (SPI_WT_1_POS_MSG \
					    + SPI_RD_DAT_DUM_TRIM)
#define SPI_RD_2_DAT_LEN_TRIM              (SPI_WT_2_POS_MSG \
					    + SPI_RD_DAT_DUM_TRIM)
#define SPI_RD_3_DAT_LEN_TRIM              (SPI_WT_3_POS_MSG \
					    + SPI_RD_DAT_DUM_TRIM)

static int32_t spi_sync_icmd(struct spi_device *client,
			     uint8_t *t_buf, uint16_t t_buf_len)
{
	struct spi_transfer t = {
		.tx_buf = t_buf,
		.rx_buf = t_buf,
		.len    = t_buf_len,
	};

	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(client, &m);
}

static int32_t spi_writef1_icmd(struct spi_device *client,
				const uint8_t icmd, uint8_t *buf,
				const uint16_t len)
{
	uint8_t *t_buf     = buf + SPI_WT_1_POS_MSG;
	uint16_t t_buf_len = len + SPI_WT_HEAD_WRAP_LEN
			     - SPI_WT_1_POS_MSG;

	buf[SPI_WT_1_POS_ICMD] = icmd;

	return spi_sync_icmd(client, t_buf, t_buf_len);
	/* TODO, handle StatusFlag in t_buf[4] */
}

static int32_t spi_writef2_icmd(struct spi_device *client,
				const uint8_t icmd, uint8_t *buf,
				const uint16_t len, const uint8_t flg)
{
	uint8_t *t_buf     = buf + SPI_WT_2_POS_MSG;
	uint16_t t_buf_len = len + SPI_WT_HEAD_WRAP_LEN - SPI_WT_2_POS_MSG;

	buf[SPI_WT_2_POS_ICMD]         = icmd;
	buf[SPI_WT_2_POS_LEN_LOW]      = get_len_low_byte(len);
	buf[SPI_WT_2_POS_FLG_LEN_HIGH] = get_flg_len_high_byte(len, flg);

	return spi_sync_icmd(client, t_buf, t_buf_len);
	/* TODO, handle StatusFlag in t_buf[4] */
}

static int32_t spi_writef3_icmd(struct spi_device *client,
				const uint8_t icmd, uint8_t *buf,
				const uint16_t len, const uint8_t ofst,
				const uint8_t flg)
{
	uint8_t *t_buf     = buf + SPI_WT_3_POS_MSG;
	uint16_t t_buf_len = len + SPI_WT_HEAD_WRAP_LEN
			     - SPI_WT_3_POS_MSG;

	buf[SPI_WT_3_POS_ICMD]         = icmd;
	buf[SPI_WT_3_POS_OFST]         = ofst;
	buf[SPI_WT_3_POS_LEN_LOW]      = get_len_low_byte(len);
	buf[SPI_WT_3_POS_FLG_LEN_HIGH] = get_flg_len_high_byte(len, flg);

	return spi_sync_icmd(client, t_buf, t_buf_len);
	/* TODO, handle StatusFlag in t_buf[4] */
}

static int32_t spi_send_icmd(void *client, const uint8_t icmd,
			     uint8_t *buf, const size_t len,
			     const uint8_t ofst, const uint8_t flg)
{
	int32_t ret = -EIO;

	switch (get_cmd_fmt(icmd)) {
	case CMD_FMT_WT_1:
	case CMD_FMT_RW_1:
		ret = spi_writef1_icmd(client, icmd, buf, len);
		break;
	case CMD_FMT_WT_2:
	case CMD_FMT_RW_2:
		ret = spi_writef2_icmd(client, icmd, buf, len, flg);
		break;
	case CMD_FMT_WT_3:
	case CMD_FMT_RW_3:
		ret = spi_writef3_icmd(client, icmd, buf, len, ofst, flg);
		break;
	case CMD_FMT_MAX:
	default:
		HIFC_ERR("%s unsupported ICMD 0x%02x\n",
		       __func__, icmd);
		break;
	}

	return ret;
}

static int32_t spi_readf0_icmd(struct spi_device *client, const uint8_t icmd)
{
	uint8_t buf = icmd;
	return spi_sync_icmd(client, &buf, sizeof(buf));
	/* TODO, handle StatusFlag in buf[2] */
}

static int32_t spi_readf1_icmd(struct spi_device *client,
			       const uint8_t icmd, uint8_t *buf,
			       const uint16_t len)
{
	int32_t ret = 0;
	uint16_t t_buf_len = SPI_RD_HEAD_WRAP_LEN + len;

	buf[SPI_RD_1_POS_ICMD] = icmd;

	ret = spi_sync_icmd(client, buf + SPI_RD_1_POS_MSG,
			    t_buf_len - SPI_RD_1_POS_MSG);

	dump_buffer(buf + SPI_RD_1_DAT_LEN_TRIM, len);

	return ret;
	/* TODO, handle StatusFlag in buf[6] */
}

static int32_t spi_readf2_icmd(struct spi_device *client,
			       const uint8_t icmd, uint8_t *buf,
			       const uint16_t len, const uint8_t flg)
{
	int32_t ret = 0;
	uint16_t t_buf_len = SPI_RD_HEAD_WRAP_LEN + len;

	buf[SPI_RD_2_POS_ICMD]         = icmd;
	buf[SPI_RD_2_POS_LEN_LOW]      = get_len_low_byte(len);
	buf[SPI_RD_2_POS_FLG_LEN_HIGH] = get_flg_len_high_byte(len, flg);

	ret = spi_sync_icmd(client, buf + SPI_RD_2_POS_MSG,
			    t_buf_len - SPI_RD_2_POS_MSG);

	return ret;
	/* TODO, handle StatusFlag in buf[6] */
}

static int32_t spi_readf3_icmd(struct spi_device *client,
			       const uint8_t icmd, uint8_t *buf,
			       const uint16_t len, const uint8_t ofst,
			       const uint8_t flg)
{
	int32_t ret = 0;
	uint16_t t_buf_len = SPI_RD_HEAD_WRAP_LEN + len;

	buf[SPI_RD_3_POS_ICMD]         = icmd;
	buf[SPI_RD_3_POS_OFST]         = ofst;
	buf[SPI_RD_3_POS_LEN_LOW]      = get_len_low_byte(len);
	buf[SPI_RD_3_POS_FLG_LEN_HIGH] = get_flg_len_high_byte(len, flg);

	ret = spi_sync_icmd(client, buf + SPI_RD_3_POS_MSG,
			    t_buf_len - SPI_RD_3_POS_MSG);

	/* TODO magic num */
	buf[7] = buf[6];

	return ret;
	/* TODO, handle StatusFlag in buf[6] */
}

static int32_t spi_recv_icmd(void *client, const uint8_t icmd,
			     uint8_t *buf , const size_t len,
			     const uint8_t ofst, const uint8_t flg,
			     uint8_t *buf_ofst)
{
	int32_t ret = -EIO;

	switch (get_cmd_fmt(icmd)) {
	case CMD_FMT_RD_0:
		ret = spi_readf0_icmd(client, icmd);
		break;
	case CMD_FMT_RD_1:
	case CMD_FMT_RW_1:
		ret = spi_readf1_icmd(client, icmd, buf, len);
		*buf_ofst = SPI_RD_1_DAT_LEN_TRIM;
		break;
	case CMD_FMT_RD_2:
	case CMD_FMT_RW_2:
		ret = spi_readf2_icmd(client, icmd, buf, len, flg);
		*buf_ofst = SPI_RD_2_DAT_LEN_TRIM;
		break;
	case CMD_FMT_RD_3:
	case CMD_FMT_RW_3:
		ret = spi_readf3_icmd(client, icmd, buf, len, ofst, flg);
		*buf_ofst = SPI_RD_3_DAT_LEN_TRIM;
		break;
	case CMD_FMT_MAX:
	default:
		HIFC_ERR("%s unsupported ICMD 0x%02x\n",
			 __func__, icmd);
		break;
	}

	return ret;
}

static void spi_enable_irq(const void *client)
{
	WARN_ON(!client);
	enable_irq(((struct spi_device *)client)->irq);
}

static const struct hifc_bus_ops spz_buf_ops = {
	.spz_drv_send_icmd  = spi_send_icmd,
	.spz_drv_recv_icmd  = spi_recv_icmd,
	.spz_drv_enable_irq = spi_enable_irq
};

static struct hifc_bus_data spz_bus_data = {
	.bops = &spz_buf_ops
};

static irqreturn_t hifc_isr(int32_t irq, void *dev_id)
{
	struct hifc_data *hdata = dev_id;
	struct spi_device *client = hdata->bdata->client;

	disable_irq_nosync(client->irq);
	schedule_work(&hdata->work);

	return IRQ_HANDLED;
}

static int32_t init_client(struct spi_device *client)
{
	return request_irq(client->irq,
			   hifc_isr,
			   IRQF_TRIGGER_HIGH,
			   "hifc_spi",
			   dev_get_drvdata(&client->dev));
}

static int32_t spz_drv_spi_probe(struct spi_device *client)
{
	int32_t ret = 0;
	uint32_t trigger_io;

	HIFC_ALERT("%s\n", __func__);

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

	spz_bus_data.client = (void *)client;

	ret = spz_prot_probe(&client->dev, &spz_bus_data);

	return ret ? ret : init_client(client);
}

static int32_t deinit_client(struct spi_device *client)
{
	free_irq(client->irq, dev_get_drvdata(&client->dev));

	return 0;
}

static int32_t spz_drv_spi_remove(struct spi_device *client)
{
	struct hifc_data *hdata =
			(struct hifc_data *)dev_get_drvdata(&client->dev);

	deinit_client(client);
	hdata->bdata = NULL;

	HIFC_ALERT("%s\n", __func__);
	return spz_prot_remove(&client->dev);
}

static const struct of_device_id of_spz_hifc_match[] = {
	{.compatible = "hifc_spi", },
	{}
};

static struct spi_driver spz_hifc_spi_driver = {
	.probe     = spz_drv_spi_probe,
	.remove    = spz_drv_spi_remove,
	.driver    = {
		.name  = "hifc_spi",
		.owner = THIS_MODULE,
		.bus   = &spi_bus_type,
		.of_match_table = of_match_ptr(of_spz_hifc_match),
	},
};

static int32_t __init spz_drv_spi_init(void)
{
	HIFC_ALERT("%s\n", __func__);
	return spi_register_driver(&spz_hifc_spi_driver);
}

static void __exit spz_drv_spi_exit(void)
{
	HIFC_ALERT("%s\n", __func__);
	return spi_unregister_driver(&spz_hifc_spi_driver);
}

module_init(spz_drv_spi_init);
module_exit(spz_drv_spi_exit);

/* MODULE_LICENSE("Proprietary"); */
MODULE_LICENSE("GPL");
