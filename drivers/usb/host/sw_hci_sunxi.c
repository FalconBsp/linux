/*
 * drivers/usb/host/sw_hci_sunxi.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * Author: javen
 * History:
 *    <author>          <time>          <version>               <desc>
 *    yangnaitian      2011-5-24            1.0          create this file
 *    javen            2011-7-18            1.1          添加了时钟开关和供电开关
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of.h>

#include <linux/regulator/consumer.h>

#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/unaligned.h>

#include "sw_hci_sunxi.h"

#define SUNXI_USB_DMA_ALIGN ARCH_DMA_MINALIGN
#define URB_ALIGNED_TEMP_SETUP 0x01000000

static u32 usb1_set_vbus_cnt;
static u32 usb2_set_vbus_cnt;


struct temp_buffer {
	void *kmalloc_ptr;
	void *old_buffer;
	u8 data[];
};

static void *alloc_temp_buffer(size_t size, gfp_t mem_flags)
{
	struct temp_buffer *temp, *kmalloc_ptr;
	size_t kmalloc_size;

	kmalloc_size = size + sizeof(struct temp_buffer) +
			SUNXI_USB_DMA_ALIGN - 1;

	kmalloc_ptr = kmalloc(kmalloc_size, mem_flags);
	if (!kmalloc_ptr)
		return NULL;

	/* Position our struct temp_buffer such that data is aligned.
	 *
	 * Note: kmalloc_ptr is type 'struct temp_buffer *' and PTR_ALIGN
	 * returns pointer with the same type 'struct temp_buffer *'.
	 */
	temp = PTR_ALIGN(kmalloc_ptr + 1, SUNXI_USB_DMA_ALIGN) - 1;

	temp->kmalloc_ptr = kmalloc_ptr;
	return temp;
}

static void sunxi_hcd_free_temp_buffer(struct urb *urb)
{
	enum dma_data_direction dir;
	struct temp_buffer *temp;

	if (!(urb->transfer_flags & URB_ALIGNED_TEMP_BUFFER))
		return;

	dir = usb_urb_dir_in(urb) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	temp = container_of(urb->transfer_buffer, struct temp_buffer, data);

	if (dir == DMA_FROM_DEVICE)
		memcpy(temp->old_buffer, temp->data,
		       urb->transfer_buffer_length);

	urb->transfer_buffer = temp->old_buffer;
	kfree(temp->kmalloc_ptr);

	urb->transfer_flags &= ~URB_ALIGNED_TEMP_BUFFER;
}


static int sunxi_hcd_alloc_temp_buffer(struct urb *urb, gfp_t mem_flags)
{
	enum dma_data_direction dir;
	struct temp_buffer *temp;

	if (urb->num_sgs)
		return 0;
	if (urb->sg)
		return 0;
	if (urb->transfer_buffer_length == 0)
		return 0;
	if (urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
		return 0;

	/* sunxi hardware requires transfer buffers to be DMA aligned */
	if (!((uintptr_t)urb->transfer_buffer & (SUNXI_USB_DMA_ALIGN - 1)))
		return 0;

	/* Allocate a buffer with enough padding for alignment */
	temp = alloc_temp_buffer(urb->transfer_buffer_length, mem_flags);
	if (!temp)
		return -ENOMEM;

	dir = usb_urb_dir_in(urb) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	temp->old_buffer = urb->transfer_buffer;
	if (dir == DMA_TO_DEVICE)
		memcpy(temp->data, urb->transfer_buffer,
		       urb->transfer_buffer_length);
	urb->transfer_buffer = temp->data;

	urb->transfer_flags |= URB_ALIGNED_TEMP_BUFFER;

	return 0;
}

static void sunxi_hcd_free_temp_setup(struct urb *urb)
{
	struct temp_buffer *temp;

	if (!(urb->transfer_flags & URB_ALIGNED_TEMP_SETUP))
		return;

	temp = container_of((void *)urb->setup_packet, struct temp_buffer,
			    data);

	urb->setup_packet = temp->old_buffer;
	kfree(temp->kmalloc_ptr);

	urb->transfer_flags &= ~URB_ALIGNED_TEMP_SETUP;


}

static int sunxi_hcd_alloc_temp_setup(struct urb *urb, gfp_t mem_flags)
{
	struct temp_buffer *temp;

	if (!usb_endpoint_xfer_control(&urb->ep->desc))
		return 0;

	/* sunxi hardware requires setup packet to be DMA aligned */
	if (!((uintptr_t)urb->setup_packet & (SUNXI_USB_DMA_ALIGN - 1)))
		return 0;

	/* Allocate a buffer with enough padding for alignment */
	temp = alloc_temp_buffer(sizeof(struct usb_ctrlrequest), mem_flags);
	if (!temp)
		return -ENOMEM;

	temp->old_buffer = urb->setup_packet;
	memcpy(temp->data, urb->setup_packet, sizeof(struct usb_ctrlrequest));
	urb->setup_packet = temp->data;

	urb->transfer_flags |= URB_ALIGNED_TEMP_SETUP;

	return 0;
}

int sunxi_hcd_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
				     gfp_t mem_flags)
{
	int ret;

	ret = sunxi_hcd_alloc_temp_buffer(urb, mem_flags);
	if (ret)
		return ret;

	ret = sunxi_hcd_alloc_temp_setup(urb, mem_flags);
	if (ret) {
		sunxi_hcd_free_temp_buffer(urb);
		return ret;
	}

	ret = usb_hcd_map_urb_for_dma(hcd, urb, mem_flags);
	if (ret) {
		sunxi_hcd_free_temp_setup(urb);
		sunxi_hcd_free_temp_buffer(urb);
		return ret;
	}

	return ret;

}
EXPORT_SYMBOL_GPL(sunxi_hcd_map_urb_for_dma);

void sunxi_hcd_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	usb_hcd_unmap_urb_for_dma(hcd, urb);
	sunxi_hcd_free_temp_setup(urb);
	sunxi_hcd_free_temp_buffer(urb);
}
EXPORT_SYMBOL_GPL(sunxi_hcd_unmap_urb_for_dma);


static s32 get_usb_cfg(struct platform_device * pdev, struct sw_hci_hcd *sw_hci)
{
	sw_hci->used = 1;
	sw_hci->vbus_regulator = devm_regulator_get(&pdev->dev, "vbus");
	if(IS_ERR(sw_hci->vbus_regulator)){
		if (PTR_ERR(sw_hci->vbus_regulator) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	}
	sw_hci->host_init_state = 1;
	return 0;
}

static void __iomem * phy_csr;
static __u32 USBC_Phy_Write(__u32 usbc_no, __u32 addr, __u32 data, __u32 len)
{
	__u32 temp = 0, dtmp = 0;
	__u32 j = 0;
	__u32 usbc_bit = 0;
	void __iomem * dest = phy_csr;

	dtmp = data;
	usbc_bit = BIT(usbc_no * 2);
	for (j = 0; j < len; j++) {
		/* set the bit address to be written */
		temp = readl(dest);
		temp &= ~(0xff << 8);
		temp |= ((addr + j) << 8);
		writel(temp, dest);

		/* clear usbc bit and set data bit */
		temp = readb(dest);
		temp &= ~usbc_bit;
		if (dtmp & 0x1)
			temp |= BIT(7);
		else
			temp &= ~BIT(7);
		writeb(temp, dest);

		/* set usbc bit */
		temp = readb(dest);
		temp |= usbc_bit;
		writeb(temp, dest);

		/* clear usbc bit */
		temp = readb(dest);
		temp &= ~usbc_bit;
		writeb(temp, dest);

		dtmp >>= 1;
	}

	return data;
}

static void UsbPhyInit(__u32 usbc_no)
{
	/* 调整 USB0 PHY 的幅度和速率 */
	USBC_Phy_Write(usbc_no, 0x20, 0x14, 5);

	/* DMSG_DEBUG("csr2-1: usbc%d: 0x%x\n", usbc_no, (u32)USBC_Phy_Read(usbc_no, 0x20, 5)); */

	/* 调节 disconnect 域值 */
		USBC_Phy_Write(usbc_no, 0x2a, 3, 2);

	/* DMSG_DEBUG("csr2: usbc%d: 0x%x\n", usbc_no, (u32)USBC_Phy_Read(usbc_no, 0x2a, 2)); */
	DMSG_DEBUG("csr3: usbc%d: 0x%x\n", usbc_no, (u32)readl(phy_csr));

	return;
}

static s32 clock_init(struct platform_device * pdev, struct sw_hci_hcd *sw_hci)
{

	sw_hci->sie_clk = devm_clk_get(&pdev->dev, "ahb_ehci");
	if (IS_ERR(sw_hci->sie_clk)) {
		DMSG_PANIC("ERR: get ehci%d ahb_ehci clk failed.\n",
			   (sw_hci->usbc_no));
		goto failed;
	}

	sw_hci->phy_gate = devm_clk_get(&pdev->dev, "usb_clk_phy");
	if (IS_ERR(sw_hci->phy_gate)) {
		DMSG_PANIC("ERR: get usb%d usb_clk_phy failed.\n",
			   sw_hci->usbc_no);
		goto failed;
	}
	// Use generic reset controller.
	sw_hci->phy_reset = reset_control_get(&pdev->dev, "usb_clk_phy_reset");
	if (IS_ERR(sw_hci->phy_reset)) {
		DMSG_PANIC("ERR: get usb%d phy_reset failed.\n",
			   sw_hci->usbc_no);
		goto failed;
	}
	return 0;

failed:
	if (sw_hci->sie_clk) {
		devm_clk_put(&pdev->dev, sw_hci->sie_clk);
		sw_hci->sie_clk = NULL;
	}

	if (sw_hci->phy_gate) {
		devm_clk_put(&pdev->dev, sw_hci->phy_gate);
		sw_hci->phy_gate = NULL;
	}

	return -1;
}

static int open_clock(struct sw_hci_hcd *sw_hci, u32 ohci)
{
	int ret;
	DMSG_INFO("[%s]: open clock\n", sw_hci->hci_name);

	if (sw_hci->sie_clk && sw_hci->phy_gate
	    && sw_hci->phy_reset && !sw_hci->clk_is_open) {
		sw_hci->clk_is_open = 1;

		ret = clk_prepare_enable(sw_hci->phy_gate);
		if (ret)
			goto fail1;

        ret = reset_control_deassert(sw_hci->phy_reset);
        if (ret)
            goto fail1;

		mdelay(10);

		clk_prepare_enable(sw_hci->sie_clk);
		if (ret)
			goto fail1;

		mdelay(10);

		UsbPhyInit(sw_hci->usbc_no);
	} else {
		DMSG_PANIC
		    ("[%s]: wrn: open clock failed, (0x%p, 0x%p, 0x%p, %d, 0x%p)\n",
		     sw_hci->hci_name, sw_hci->sie_clk, sw_hci->phy_gate,
		     sw_hci->phy_reset, sw_hci->clk_is_open, sw_hci->ohci_gate);
	}
/*TODO: Impelement failure return*/
fail1:
	return 0;
}

static int close_clock(struct sw_hci_hcd *sw_hci, u32 ohci)
{
	DMSG_INFO("[%s]: close clock\n", sw_hci->hci_name);

	if (sw_hci->sie_clk && sw_hci->phy_gate
	    && sw_hci->phy_reset && sw_hci->clk_is_open) {

		sw_hci->clk_is_open = 0;

		reset_control_assert(sw_hci->phy_reset);
		clk_disable(sw_hci->phy_gate);
		clk_disable(sw_hci->sie_clk);
	} else {
		DMSG_PANIC
		    ("[%s]: wrn: open clock failed, (0x%p, 0x%p, 0x%p, %d, 0x%p)\n",
		     sw_hci->hci_name, sw_hci->sie_clk, sw_hci->phy_gate,
		     sw_hci->phy_reset, sw_hci->clk_is_open, sw_hci->ohci_gate);
	}

	return 0;
}

static void usb_passby(struct sw_hci_hcd *sw_hci, u32 enable)
{
	unsigned long reg_value = 0;
	unsigned long bits = 0;
	static DEFINE_SPINLOCK(lock);
	unsigned long flags = 0;


	spin_lock_irqsave(&lock, flags);

	bits =	BIT(10) | /* AHB Master interface INCR8 enable */
			BIT(9)  | /* AHB Master interface burst type INCR4 enable */
			BIT(8)  | /* AHB Master interface INCRX align enable */
			BIT(0);   /* ULPI bypass enable */

	reg_value = readl(sw_hci->usb_vbase);

	if (enable)
		reg_value |= bits;
	else
		reg_value &= ~bits;

	writel(reg_value, sw_hci->usb_vbase);

	spin_unlock_irqrestore(&lock, flags);

	return;
}

static int csr_ioremaped;
static int dram_base_ioremaped;
static void __iomem * dram_base;
static void hci_port_configure(struct sw_hci_hcd *sw_hci, u32 enable)
{
	unsigned long reg_value = 0;
	u32 usbc_sdram_hpcr = 0;
	void __iomem *addr = NULL;

	if (sw_hci->usbc_no == 1) {
		usbc_sdram_hpcr = SW_SDRAM_REG_HPCR_USB1;
	} else if (sw_hci->usbc_no == 2) {
		usbc_sdram_hpcr = SW_SDRAM_REG_HPCR_USB2;
	} else {
		DMSG_PANIC("EER: unkown usbc_no(%d)\n", sw_hci->usbc_no);
		return;
	}

	addr = (void __iomem*) dram_base + usbc_sdram_hpcr;

	reg_value = readl(addr);
	if (enable)
		reg_value |= BIT(SW_SDRAM_BP_HPCR_ACCESS_EN);
	else
		reg_value &= ~BIT(SW_SDRAM_BP_HPCR_ACCESS_EN);

	writel(reg_value, addr);
	return;
}


static void sw_set_vbus(struct sw_hci_hcd *sw_hci, int is_on)
{
	int ret = 0;

	DMSG_DEBUG("[%s]: sw_set_vbus cnt %d\n",
		   sw_hci->hci_name,
		   (sw_hci->usbc_no ==
		    1) ? usb1_set_vbus_cnt : usb2_set_vbus_cnt);

	if (sw_hci->usbc_no == 1) {
		if (is_on && usb1_set_vbus_cnt == 0)
			ret = regulator_enable(sw_hci->vbus_regulator);
		else if (!is_on && usb1_set_vbus_cnt == 1)
			ret = regulator_disable(sw_hci->vbus_regulator);

		if (ret != 0) {
			DMSG_PANIC("error [%s]:regulator_disable failed: %d\n",
					sw_hci->hci_name, ret);
			return;
		}

		if (is_on)
			usb1_set_vbus_cnt++;
		else
			usb1_set_vbus_cnt--;
	} else {
		if (is_on && usb2_set_vbus_cnt == 0)
			ret = regulator_enable(sw_hci->vbus_regulator);
		else if (!is_on && usb2_set_vbus_cnt == 1)
			ret = regulator_disable(sw_hci->vbus_regulator);

		if (ret != 0) {
			DMSG_PANIC("error [%s]:regulator_disable failed: %d\n",
				sw_hci->hci_name, ret);
			return;
		}

		if (is_on)
			usb2_set_vbus_cnt++;
		else
			usb2_set_vbus_cnt--;
	}

	return;
}

/*
 *---------------------------------------------------------------
 * EHCI
 *---------------------------------------------------------------
 */

#define  SW_EHCI_NAME		"sw-ehci"
static const char ehci_name[] = SW_EHCI_NAME;

static struct sw_hci_hcd sw_ehci1;
static struct sw_hci_hcd sw_ehci2;
/*
 *---------------------------------------------------------------
 * OHCI
 *---------------------------------------------------------------
 */

#define  SW_OHCI_NAME		"sw-ohci"
static const char ohci_name[] = SW_OHCI_NAME;

//static struct sw_hci_hcd sw_ohci1;
//static struct sw_hci_hcd sw_ohci2;

static void print_sw_hci(struct sw_hci_hcd *sw_hci)
{
	DMSG_DEBUG("\n------%s config------\n", sw_hci->hci_name);
	DMSG_DEBUG("hci_name             = %s\n", sw_hci->hci_name);
	DMSG_DEBUG("usbc_no              = %d\n", sw_hci->usbc_no);

	DMSG_DEBUG("usb_vbase            = 0x%p\n", sw_hci->usb_vbase);

	DMSG_DEBUG("used                 = %d\n", sw_hci->used);
	DMSG_DEBUG("host_init_state      = %d\n", sw_hci->host_init_state);

	DMSG_DEBUG("gpio_name            = %s\n",
		   sw_hci->drv_vbus_gpio_set.gpio_name);
	DMSG_DEBUG("port                 = %d\n",
		   sw_hci->drv_vbus_gpio_set.port);
	DMSG_DEBUG("port_num             = %d\n",
		   sw_hci->drv_vbus_gpio_set.port_num);
	DMSG_DEBUG("mul_sel              = %d\n",
		   sw_hci->drv_vbus_gpio_set.mul_sel);
	DMSG_DEBUG("pull                 = %d\n",
		   sw_hci->drv_vbus_gpio_set.pull);
	DMSG_DEBUG("drv_level            = %d\n",
		   sw_hci->drv_vbus_gpio_set.drv_level);
	DMSG_DEBUG("data                 = %d\n",
		   sw_hci->drv_vbus_gpio_set.data);

	DMSG_DEBUG("\n--------------------------\n");

	return;
}

struct sw_hci_hcd * sunxi_prepare_sw_hci_hcd(struct platform_device * pdev, int * result)
{
	s32 ret = 0;
	struct sw_hci_hcd * sw_hci;
	struct resource *res;
	struct device_node *dn = pdev->dev.of_node;
	int usb_no = 0;

	if(!dn)
		return NULL;

	of_property_read_u32(dn, "sunxi-no", &usb_no);

	if (usb_no == 1){
		sw_hci =  &sw_ehci1;
		memset(sw_hci, 0, sizeof(struct sw_hci_hcd));
		sw_hci->usbc_no = 1;
	}
	else if (usb_no ==2){
		sw_hci =  &sw_ehci2;
		memset(sw_hci, 0, sizeof(struct sw_hci_hcd));
		sw_hci->usbc_no = 2;
	}else
		return NULL;

	sprintf(sw_hci->hci_name, "%s%d", ehci_name, sw_hci->usbc_no);


	 ret = get_usb_cfg(pdev, sw_hci);
	 if(ret){
		*result = ret;
		return NULL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		pr_err("%s: failed to get io memory\n", __func__);
		return NULL;
	}
	sw_hci->usb_vbase  = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sw_hci->usb_vbase))
		return NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!res) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		return NULL;
	}

	if(!csr_ioremaped){
		phy_csr = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(phy_csr)) {
			return NULL;
		}
		csr_ioremaped = 1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (!res) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		return NULL;
	}

	if(!dram_base_ioremaped){
		dram_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(dram_base)) {
			return NULL;
		}
		dram_base_ioremaped = 1;
	}

	sw_hci->open_clock = open_clock;
	sw_hci->close_clock = close_clock;
	sw_hci->set_power = sw_set_vbus;
	sw_hci->usb_passby = usb_passby;
	sw_hci->port_configure = hci_port_configure;

	ret = clock_init(pdev, sw_hci);
	if (ret != 0) {
		DMSG_PANIC("ERR: clock_init failed\n");
		goto failed2;
	}

	print_sw_hci(sw_hci);

	return sw_hci;

failed2:
	return NULL;
}
