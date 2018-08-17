#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
//#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/interrupt.h>

#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>         /* wake_lock, unlock */
#include <linux/version.h>          /* check linux version */

#include "../../broadcast_tdmb_drv_ifdef.h"
#include "../inc/broadcast_fc8080.h"
#include "../inc/fci_types.h"
#include "../inc/bbm.h"

#include <linux/err.h>
#include <linux/of_gpio.h>

#include <linux/clk.h>
#define FEATURE_DMB_USE_PINCTRL
#ifdef FEATURE_DMB_USE_PINCTRL
#include <linux/pinctrl/consumer.h>
#endif

#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>

/* external function */
extern int broadcast_fc8080_drv_if_isr(void);
extern void tunerbb_drv_fc8080_isr_control(fci_u8 onoff);

/* proto type declare */
static int broadcast_tdmb_fc8080_probe(struct spi_device *spi);
static int broadcast_tdmb_fc8080_remove(struct spi_device *spi);

#define LGE_FC8080_DRV_VER  "1.00.12"

/************************************************************************/
/* LINUX Driver Setting                                                 */
/************************************************************************/
static uint32 user_stop_flg = 0;
struct tdmb_fc8080_ctrl_blk
{
    boolean                                    TdmbPowerOnState;
    struct spi_device*                    spi_ptr;
    struct mutex                            mutex;
    struct wake_lock                      wake_lock;    /* wake_lock,wake_unlock */
    boolean                                    spi_irq_status;
    spinlock_t                                 spin_lock;
    struct clk                                *clk;
    struct platform_device             *pdev;

    uint32                                      dmb_en;
    uint32                                      dmb_irq;
    uint32                                      dmb_use_xtal;
    uint32                                      dmb_xtal_freq;
    uint32                                      dmb_interface_freq;

    uint32                                      dmb_use_ant_sw;
    uint32                                      dmb_ant_active_mode;
    uint32                                      dmb_ant;
    uint32                                      ctrl_dmb_ldo;
    struct regulator                         *dmb_ldo;
    uint32                                      ctrl_lna_ldo;
    struct regulator                         *lna_ldo;

    uint32                                      dmb_use_lna_ctrl;
    uint32                                      dmb_lna_ctrl;
    uint32                                      dmb_use_lna_en;
    uint32                                      dmb_lna_en;
};

static struct tdmb_fc8080_ctrl_blk fc8080_ctrl_info;

static Device_drv device_fc8080 = {
    &broadcast_fc8080_drv_if_power_on,
    &broadcast_fc8080_drv_if_power_off,
    &broadcast_fc8080_drv_if_init,
    &broadcast_fc8080_drv_if_stop,
    &broadcast_fc8080_drv_if_set_channel,
    &broadcast_fc8080_drv_if_detect_sync,
    &broadcast_fc8080_drv_if_get_sig_info,
    &broadcast_fc8080_drv_if_get_fic,
    &broadcast_fc8080_drv_if_get_msc,
    &broadcast_fc8080_drv_if_reset_ch,
    &broadcast_fc8080_drv_if_user_stop,
    &broadcast_fc8080_drv_if_select_antenna,
    &broadcast_fc8080_drv_if_set_nation,
    &broadcast_fc8080_drv_if_is_on
};

#if 0
void tdmb_fc8080_spi_write_read_test(void)
{
    uint16 i;
    uint32 wdata = 0;
    uint32 ldata = 0;
    uint32 data = 0;
    uint32 temp = 0;

#define TEST_CNT    5
    //tdmb_fc8080_power_on();

    for(i=0;i<TEST_CNT;i++)
    {
        bbm_com_write(NULL, 0xa4, i & 0xff);
        bbm_com_read(NULL, 0xa4, (fci_u8*)&data);
        printk("FC8080 byte test (0x%x,0x%x)\n", i & 0xff, data);
        if((i & 0xff) != data)
            printk("FC8080 byte test (0x%x,0x%x)\n", i & 0xff, data);
    }

    for(i=0;i<TEST_CNT;i++)
    {
        bbm_com_word_write(NULL, 0xa4, i & 0xffff);
        bbm_com_word_read(NULL, 0xa4, (fci_u16*)&wdata);
        printk("FC8080 word test (0x%x,0x%x)\n", i & 0xffff, wdata);
        if((i & 0xffff) != wdata)
            printk("FC8080 word test (0x%x,0x%x)\n", i & 0xffff, wdata);
    }

    for(i=0;i<TEST_CNT;i++)
    {
        bbm_com_long_write(NULL, 0xa4, i & 0xffffffff);
        bbm_com_long_read(NULL, 0xa4, (fci_u32*)&ldata);
        printk("FC8080 long test (0x%x,0x%x)\n", i & 0xffffffff, ldata);
        if((i & 0xffffffff) != ldata)
            printk("FC8080 long test (0x%x,0x%x)\n", i & 0xffffffff, ldata);
    }

    data = 0;

    for(i=0;i<TEST_CNT;i++)
    {
        temp = i&0xff;
        bbm_com_tuner_write(NULL, 0x58, 0x01, (fci_u8*)&temp, 0x01);
        bbm_com_tuner_read(NULL, 0x58, 0x01, (fci_u8*)&data, 0x01);
        printk("FC8080 tuner test (0x%x,0x%x)\n", i & 0xff, data);
        if((i & 0xff) != data)
            printk("FC8080 tuner test (0x%x,0x%x)\n", i & 0xff, data);
    }
    //tdmb_fc8080_power_off();
}
#endif

struct spi_device *tdmb_fc8080_get_spi_device(void)
{
    return fc8080_ctrl_info.spi_ptr;
}

void tdmb_fc8080_set_userstop(int mode)
{
    user_stop_flg = mode;
    printk("tdmb_fc8080_set_userstop, user_stop_flg = %d \n", user_stop_flg);
}

int tdmb_fc8080_mdelay(int32 ms)
{
    int32    wait_loop =0;
    int32    wait_ms = ms;
    int        rc = 1;  /* 0 : false, 1 : true */

    if(ms > 100)
    {
        wait_loop = (ms /100);   /* 100, 200, 300 more only , Otherwise this must be modified e.g (ms + 40)/50 */
        wait_ms = 100;
    }

    do
    {
        mdelay(wait_ms);

        if(user_stop_flg == 1)
        {
            printk("~~~~~~~~ Ustop flag is set so return false ms =(%d)~~~~~~~\n", ms);
            rc = 0;
            break;
        }
    }while((--wait_loop) > 0);

    return rc;
}

void tdmb_fc8080_Must_mdelay(int32 ms)
{
    mdelay(ms);
}

int tdmb_fc8080_tdmb_is_on(void)
{
    return (int)fc8080_ctrl_info.TdmbPowerOnState;
}

unsigned int tdmb_fc8080_get_xtal_freq(void)
{
    return (unsigned int)fc8080_ctrl_info.dmb_xtal_freq;
}

#ifdef FEATURE_POWER_ON_RETRY
int tdmb_fc8080_power_on_retry(void)
{
    int res;
    int i;

    tdmb_fc8080_interrupt_lock();

    for(i = 0 ; i < 10; i++)
    {
        printk("[FC8080] tdmb_fc8080_power_on_retry :  %d\n", i);

        if(!fc8080_ctrl_info.dmb_use_xtal) {
            if(fc8080_ctrl_info.clk != NULL) {
                clk_disable_unprepare(fc8080_ctrl_info.clk);
                printk("[FC8080] retry clk_disable %d\n", i);
            }
            else
                printk("[FC8080] ERR fc8080_ctrl_info.clkdis is NULL\n");
        }

        gpio_set_value(fc8080_ctrl_info.dmb_en, 0);
        mdelay(150);

        gpio_set_value(fc8080_ctrl_info.dmb_en, 1);
        mdelay(5);

        if(!fc8080_ctrl_info.dmb_use_xtal) {
            if(fc8080_ctrl_info.clk != NULL) {
                res = clk_prepare_enable(fc8080_ctrl_info.clk);
                if (res) {
                    printk("[FC8080] retry clk_prepare_enable fail %d\n", i);
                }
            }
            else
                printk("[FC8080] ERR fc8080_ctrl_info.clken is NULL\n");
        }
        mdelay(30);

        res = bbm_com_probe(NULL);

        if (!res)
            break;

    }

    tdmb_fc8080_interrupt_free();

    return res;
}
#endif

int tdmb_fc8080_power_on(void)
{
    int rc = FALSE;

    if(fc8080_ctrl_info.ctrl_dmb_ldo) {
        rc = regulator_enable(fc8080_ctrl_info.dmb_ldo);
        if(rc) {
            dev_err(&fc8080_ctrl_info.spi_ptr->dev, "unable to enable dmb_ldo\n");
            return rc;
        }
        else {
            printk("dmb_ldo enable\n");
        }
        mdelay(5);
    }

    if(fc8080_ctrl_info.ctrl_lna_ldo) {
        rc = regulator_enable(fc8080_ctrl_info.lna_ldo);
        if(rc) {
            dev_err(&fc8080_ctrl_info.spi_ptr->dev, "unable to enable lna_ldo\n");
            return rc;
        }
        else {
            printk("lna_ldo enable\n");
        }
        mdelay(5);
    }

    printk("tdmb_fc8080_power_on \n");
    if ( fc8080_ctrl_info.TdmbPowerOnState == FALSE )
    {
        wake_lock(&fc8080_ctrl_info.wake_lock);

        gpio_set_value(fc8080_ctrl_info.dmb_en, 0);
        mdelay(5);
        gpio_set_value(fc8080_ctrl_info.dmb_en, 1);
        mdelay(5);

        if(fc8080_ctrl_info.dmb_use_lna_ctrl)
        {
            gpio_set_value(fc8080_ctrl_info.dmb_lna_ctrl, 0);
        }

        if(fc8080_ctrl_info.dmb_use_lna_en)
        {
            gpio_set_value(fc8080_ctrl_info.dmb_lna_en, 1);
        }

        if(!fc8080_ctrl_info.dmb_use_xtal) {
            if(fc8080_ctrl_info.clk != NULL) {
                rc = clk_prepare_enable(fc8080_ctrl_info.clk);
                if (rc) {
                    gpio_set_value(fc8080_ctrl_info.dmb_en, 0);
                    mdelay(5);
                    dev_err(&fc8080_ctrl_info.spi_ptr->dev, "could not enable clock\n");
                    return rc;
                }
            }
        }

        if(fc8080_ctrl_info.dmb_use_ant_sw)
        {
            gpio_set_value(fc8080_ctrl_info.dmb_ant, fc8080_ctrl_info.dmb_ant_active_mode);
            printk("tdmb_ant_sw_gpio_value : %d\n", fc8080_ctrl_info.dmb_ant_active_mode);
        }
        mdelay(30); /* Due to X-tal stablization Time */

        tdmb_fc8080_interrupt_free();
        fc8080_ctrl_info.TdmbPowerOnState = TRUE;

        printk("tdmb_fc8080_power_on OK\n");
    }
    else
    {
        printk("tdmb_fc8080_power_on the power already turn on \n");
    }

    printk("tdmb_fc8080_power_on completed \n");
    rc = TRUE;

    return rc;
}

int tdmb_fc8080_power_off(void)
{
    if ( fc8080_ctrl_info.TdmbPowerOnState == TRUE )
    {
        tdmb_fc8080_interrupt_lock();

        if(!fc8080_ctrl_info.dmb_use_xtal) {
            if(fc8080_ctrl_info.clk != NULL) {
                clk_disable_unprepare(fc8080_ctrl_info.clk);
            }
        }

        fc8080_ctrl_info.TdmbPowerOnState = FALSE;

        gpio_set_value(fc8080_ctrl_info.dmb_en, 0);

        wake_unlock(&fc8080_ctrl_info.wake_lock);
        mdelay(20);

        if(fc8080_ctrl_info.dmb_use_ant_sw)
        {
            gpio_set_value(fc8080_ctrl_info.dmb_ant, !(fc8080_ctrl_info.dmb_ant_active_mode));
            printk("tdmb_ant_sw_gpio_value : %d\n", !(fc8080_ctrl_info.dmb_ant_active_mode));
        }

        if(fc8080_ctrl_info.dmb_use_lna_ctrl)
        {
            gpio_set_value(fc8080_ctrl_info.dmb_lna_ctrl, 0);
        }

        if(fc8080_ctrl_info.dmb_use_lna_en)
        {
            gpio_set_value(fc8080_ctrl_info.dmb_lna_en, 0);
        }

        if(fc8080_ctrl_info.ctrl_dmb_ldo) {
            regulator_disable(fc8080_ctrl_info.dmb_ldo);
            printk("dmb_ldo disable\n");
            mdelay(5);
        }

        if(fc8080_ctrl_info.ctrl_lna_ldo) {
            regulator_disable(fc8080_ctrl_info.lna_ldo);
            printk("lna_ldo disable\n");
            mdelay(5);
        }
    }
    else
    {
        printk("tdmb_fc8080_power_on the power already turn off \n");
    }

    printk("tdmb_fc8080_power_off completed \n");

    return TRUE;
}

int tdmb_fc8080_select_antenna(unsigned int sel)
{
    return FALSE;
}

static struct of_device_id tdmb_spi_table[] = {
    {
        .compatible = "lge,tdmb",
    },
    {}
};

static struct spi_driver broadcast_tdmb_driver = {
    .probe = broadcast_tdmb_fc8080_probe,
    .remove    = __broadcast_dev_exit_p(broadcast_tdmb_fc8080_remove),
    .driver = {
        .name = "tdmb",
        .of_match_table = tdmb_spi_table,
        .bus    = &spi_bus_type,
        .owner = THIS_MODULE,
    },
};

void tdmb_fc8080_interrupt_lock(void)
{
    if (fc8080_ctrl_info.spi_ptr == NULL)
    {
        printk("tdmb_fc8080_interrupt_lock fail\n");
    }
    else
    {
        disable_irq(fc8080_ctrl_info.spi_ptr->irq);
    }
}

void tdmb_fc8080_interrupt_free(void)
{
    if (fc8080_ctrl_info.spi_ptr == NULL)
    {
        printk("tdmb_fc8080_interrupt_free fail\n");
    }
    else
    {
        enable_irq(fc8080_ctrl_info.spi_ptr->irq);
    }
}

int tdmb_fc8080_spi_write_read(uint8* tx_data, int tx_length, uint8 *rx_data, int rx_length)
{
    int rc;

    struct spi_transfer    t = {
            .tx_buf        = tx_data,
            .rx_buf        = rx_data,
            .len        = tx_length+rx_length,
        };

    struct spi_message    m;

    if (fc8080_ctrl_info.spi_ptr == NULL)
    {
        printk("tdmb_fc8080_spi_write_read error txdata=%p, length=%d\n", (void *)tx_data, tx_length+rx_length);
        return FALSE;
    }

    mutex_lock(&fc8080_ctrl_info.mutex);

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    rc = spi_sync(fc8080_ctrl_info.spi_ptr, &m);

    if ( rc < 0 )
    {
        printk("tdmb_fc8080_spi_read_burst result(%d), actual_len=%d\n",rc, m.actual_length);
    }

    mutex_unlock(&fc8080_ctrl_info.mutex);

    return TRUE;
}

static irqreturn_t broadcast_tdmb_spi_event_handler(int irq, void *handle)
{
    struct tdmb_fc8080_ctrl_blk* fc8080_info_p;

    fc8080_info_p = (struct tdmb_fc8080_ctrl_blk *)handle;
    if ( fc8080_info_p && fc8080_info_p->TdmbPowerOnState )
    {
        if (fc8080_info_p->spi_irq_status)
        {
            printk("######### spi read function is so late skip ignore #########\n");
            return IRQ_HANDLED;
        }

        tunerbb_drv_fc8080_isr_control(0);
        fc8080_info_p->spi_irq_status = TRUE;
        broadcast_fc8080_drv_if_isr();
        fc8080_info_p->spi_irq_status = FALSE;
        tunerbb_drv_fc8080_isr_control(1);
    }
    else
    {
        printk("broadcast_tdmb_spi_isr is called, but device is off state\n");
    }

    return IRQ_HANDLED;
}

#ifdef FEATURE_DMB_USE_PINCTRL
static int tdmb_pinctrl_init(void)
{
    struct pinctrl *tdmb_pinctrl;
    tdmb_pinctrl = devm_pinctrl_get(&(fc8080_ctrl_info.pdev->dev));

    if(IS_ERR_OR_NULL(tdmb_pinctrl)) {
        pr_err("%s: Getting pinctrl handle failed\n", __func__);
        return -EINVAL;
    }
    return 0;
}
#endif

static int tunerbb_drv_load_hw_configure(struct spi_device *spi)
{
    int rc = OK;
    int err_count = 0;

    fc8080_ctrl_info.TdmbPowerOnState = FALSE;
    fc8080_ctrl_info.spi_ptr                 = spi;
    fc8080_ctrl_info.spi_ptr->mode             = SPI_MODE_0;
    fc8080_ctrl_info.spi_ptr->bits_per_word     = 8;
    fc8080_ctrl_info.pdev = to_platform_device(&spi->dev);

    fc8080_ctrl_info.dmb_en = of_get_named_gpio(fc8080_ctrl_info.pdev->dev.of_node,"tdmb-fc8080,en-gpio",0);

    rc = gpio_request(fc8080_ctrl_info.dmb_en, "DMB_EN");
    if (rc < 0) {
        err_count++;
        printk("%s:Failed GPIO DMB_EN request!!!\n",__func__);
    }

    fc8080_ctrl_info.dmb_irq = of_get_named_gpio(fc8080_ctrl_info.pdev->dev.of_node,"tdmb-fc8080,irq-gpio",0);

    rc = gpio_request(fc8080_ctrl_info.dmb_irq, "DMB_INT_N");
    if (rc < 0) {
        err_count++;
        printk("%s:Failed GPIO DMB_INT_N request!!!\n",__func__);
    }

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "use-xtal", &fc8080_ctrl_info.dmb_use_xtal);
    printk("%s: use_xtal : %d\n", __func__, fc8080_ctrl_info.dmb_use_xtal);

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "xtal-freq", &fc8080_ctrl_info.dmb_xtal_freq);
    printk("%s: dmb_xtal_freq : %d\n", __func__, fc8080_ctrl_info.dmb_xtal_freq);

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "interface-freq", &fc8080_ctrl_info.dmb_interface_freq);
    fc8080_ctrl_info.spi_ptr->max_speed_hz     = (fc8080_ctrl_info.dmb_interface_freq*1000);
    printk("%s: dmb_interface_freq : %d\n", __func__, fc8080_ctrl_info.dmb_interface_freq);

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "use-ant-sw", &fc8080_ctrl_info.dmb_use_ant_sw);
    printk("%s: dmb_use_ant_sw : %d\n", __func__, fc8080_ctrl_info.dmb_use_ant_sw);

    if(fc8080_ctrl_info.dmb_use_ant_sw) {
        of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "ant-active-mode", &fc8080_ctrl_info.dmb_ant_active_mode);
    	printk("%s: dmb_ant_active_mode : %d\n", __func__, fc8080_ctrl_info.dmb_ant_active_mode);

        fc8080_ctrl_info.dmb_ant = of_get_named_gpio(fc8080_ctrl_info.pdev->dev.of_node,"tdmb-fc8080,ant-gpio",0);
        rc = gpio_request(fc8080_ctrl_info.dmb_ant, "DMB_ANT");
        if (rc < 0) {
            err_count++;
            printk("%s: Failed GPIO DMB_ANT request!!!\n",__func__);
        }
        gpio_direction_output(fc8080_ctrl_info.dmb_ant, 0);
    }

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "ctrl-dmb-ldo", &fc8080_ctrl_info.ctrl_dmb_ldo);
    printk("%s: ctrl-dmb-ldo : %d\n", __func__, fc8080_ctrl_info.ctrl_dmb_ldo);

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "ctrl-lna-ldo", &fc8080_ctrl_info.ctrl_lna_ldo);
    printk("%s: ctrl-lna-ldo : %d\n", __func__, fc8080_ctrl_info.ctrl_lna_ldo);

    gpio_direction_output(fc8080_ctrl_info.dmb_en, 0);
    gpio_direction_input(fc8080_ctrl_info.dmb_irq);

    if(err_count > 0) rc = -EINVAL;

    if(fc8080_ctrl_info.ctrl_dmb_ldo) {
        fc8080_ctrl_info.dmb_ldo = devm_regulator_get(&fc8080_ctrl_info.spi_ptr->dev, "dmb_ldo");
        if(IS_ERR(fc8080_ctrl_info.dmb_ldo)) {
            rc = PTR_ERR(fc8080_ctrl_info.dmb_ldo);
            dev_err(&fc8080_ctrl_info.spi_ptr->dev, "regulator dmb_ldo failed\n");
        }
        else {
            printk("dmb_ldo regulator OK\n");
        }
    }

    if(fc8080_ctrl_info.ctrl_lna_ldo) {
        fc8080_ctrl_info.lna_ldo= devm_regulator_get(&fc8080_ctrl_info.spi_ptr->dev, "lna_ldo");
        if(IS_ERR(fc8080_ctrl_info.lna_ldo)) {
            rc = PTR_ERR(fc8080_ctrl_info.lna_ldo);
            dev_err(&fc8080_ctrl_info.spi_ptr->dev, "regulator lna_ldo failed\n");
        }
        else {
            printk("lna_ldo regulator OK\n");
        }
    }

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "use-lna-ctrl", &fc8080_ctrl_info.dmb_use_lna_ctrl);
    printk("%s: dmb_use_lna_ctrl : %d\n", __func__, fc8080_ctrl_info.dmb_use_lna_ctrl);

    if(fc8080_ctrl_info.dmb_use_lna_ctrl) {
        fc8080_ctrl_info.dmb_lna_ctrl = of_get_named_gpio(fc8080_ctrl_info.pdev->dev.of_node,"tdmb-fc8080,lna-ctrl-gpio",0);
        rc = gpio_request(fc8080_ctrl_info.dmb_lna_ctrl, "DMB_LNA_CTRL");
        if (rc < 0) {
            err_count++;
            printk("%s: Failed GPIO DMB_LNA_CTRL request!!!\n",__func__);
        } else {
            printk("%s: Successed GPIO DMB_LNA_CTRL %d request!!!\n",__func__, fc8080_ctrl_info.dmb_lna_ctrl);
        }
        gpio_direction_output(fc8080_ctrl_info.dmb_lna_ctrl,0);
    }

    of_property_read_u32(fc8080_ctrl_info.pdev->dev.of_node, "use-lna-en", &fc8080_ctrl_info.dmb_use_lna_en);
    printk("%s: dmb_use_lna_en : %d\n", __func__, fc8080_ctrl_info.dmb_use_lna_en);

    if(fc8080_ctrl_info.dmb_use_lna_en) {
        fc8080_ctrl_info.dmb_lna_en = of_get_named_gpio(fc8080_ctrl_info.pdev->dev.of_node,"tdmb-fc8080,lna-en-gpio",0);
        rc = gpio_request(fc8080_ctrl_info.dmb_lna_en , "DMB_LNA_EN");
        if (rc < 0) {
            err_count++;
            printk("%s: Failed GPIO DMB_LNA_EN request!!!\n",__func__);
        } else {
            printk("%s: Successed GPIO DMB_LNA_EN %d request!!!\n",__func__, fc8080_ctrl_info.dmb_lna_en);
        }
        gpio_direction_output(fc8080_ctrl_info.dmb_lna_en ,0);
    }

    return rc;
}

static int broadcast_tdmb_fc8080_probe(struct spi_device *spi)
{
    int rc;

    if(spi == NULL)
    {
        printk("%s: spi is NULL, so spi can not be set\n", __func__);
        return -1;
    }

    tunerbb_drv_load_hw_configure(spi);

    // Once I have a spi_device structure I can do a transfer anytime

    rc = spi_setup(spi);
    printk("%s: spi_setup=%d\n", __func__, rc);
    bbm_com_hostif_select(NULL, 1);

    if(!fc8080_ctrl_info.dmb_use_xtal) {
        fc8080_ctrl_info.clk = clk_get(&fc8080_ctrl_info.spi_ptr->dev, "tdmb_xo");
        if (IS_ERR(fc8080_ctrl_info.clk)) {
            rc = PTR_ERR(fc8080_ctrl_info.clk);
            dev_err(&fc8080_ctrl_info.spi_ptr->dev, "could not get clock\n");
            return rc;
        }

        /* We enable/disable the clock only to assure it works */
        rc = clk_prepare_enable(fc8080_ctrl_info.clk);
        if (rc) {
            dev_err(&fc8080_ctrl_info.spi_ptr->dev, "could not enable clock\n");
            return rc;
        }
        clk_disable_unprepare(fc8080_ctrl_info.clk);
    }

#ifdef FEATURE_DMB_USE_PINCTRL
    tdmb_pinctrl_init();
#endif

    rc = request_threaded_irq(spi->irq, NULL, broadcast_tdmb_spi_event_handler, IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
                       spi->dev.driver->name, &fc8080_ctrl_info);

    printk("%s: request_irq=%d\n", __func__, rc);

    tdmb_fc8080_interrupt_lock();

    mutex_init(&fc8080_ctrl_info.mutex);

    wake_lock_init(&fc8080_ctrl_info.wake_lock,  WAKE_LOCK_SUSPEND, dev_name(&spi->dev));

    spin_lock_init(&fc8080_ctrl_info.spin_lock);

    printk("%s: End\n", __func__);

    return rc;
}

static int broadcast_tdmb_fc8080_remove(struct spi_device *spi)
{
    printk("broadcast_tdmb_fc8080_remove \n");

    free_irq(spi->irq, &fc8080_ctrl_info);

    mutex_destroy(&fc8080_ctrl_info.mutex);

    wake_lock_destroy(&fc8080_ctrl_info.wake_lock);

    memset((unsigned char*)&fc8080_ctrl_info, 0x0, sizeof(struct tdmb_fc8080_ctrl_blk));
    return 0;
}

#if 0
static int broadcast_tdmb_fc8080_check_chip_id(void)
{
    int rc = ERROR;

    rc = tdmb_fc8080_power_on();

    if(rc == TRUE && !bbm_com_probe(NULL)) //rc == TRUE : power on success,  OK : 0
        rc = OK;
    else
        rc = ERROR;

    tdmb_fc8080_power_off();

    return rc;
}
#endif
int __broadcast_dev_init broadcast_tdmb_fc8080_drv_init(void)
{
    int rc;

    if(broadcast_tdmb_drv_check_module_init() != OK) {
        rc = ERROR;
        return rc;
    }

    rc = spi_register_driver(&broadcast_tdmb_driver);

#if 0
    if(broadcast_tdmb_fc8080_check_chip_id() != OK) {
        spi_unregister_driver(&broadcast_tdmb_driver);
        rc = ERROR;
        return rc;
    }
#endif

    rc = broadcast_tdmb_drv_start(&device_fc8080);
    printk("FC8080 DRV_VER : 20130808, BBM_VER : 1.5.1 \n");
    printk("Linux Version : %d\n", LINUX_VERSION_CODE);
    printk("LGE_FC8080_DRV_VER : %s\n", LGE_FC8080_DRV_VER);
    printk("broadcast_tdmb_fc8080_probe start %d \n", rc);

    return rc;
}

static void __exit broadcast_tdmb_fc8080_drv_exit(void)
{
    spi_unregister_driver(&broadcast_tdmb_driver);
}

module_init(broadcast_tdmb_fc8080_drv_init);
module_exit(broadcast_tdmb_fc8080_drv_exit);

/* optional part when we include driver code to build-on
it's just used when we make device driver to module(.ko)
so it doesn't work in build-on */
MODULE_DESCRIPTION("FC8080 tdmb device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FCI");
