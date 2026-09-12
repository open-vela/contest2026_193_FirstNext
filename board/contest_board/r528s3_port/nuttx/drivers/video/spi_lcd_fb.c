#ifndef LCD_SPI_FREQ
#  define LCD_SPI_FREQ 60000000
#endif


#include <debug.h>
#include <nuttx/config.h>
#include <nuttx/video/fb.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>
#include <nuttx/kthread.h>
#include <syslog.h>

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct spi_lcd_fb_s
{
  struct fb_vtable_s vtable;
  struct fb_planeinfo_s planeinfo;
  struct fb_videoinfo_s videoinfo;
  struct spi_dev_s *spi;
  void *unalign_fb;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct spi_lcd_fb_s *g_spi_lcd_fb;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int spi_lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                 FAR struct fb_videoinfo_s *vinfo);
static int spi_lcd_getplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                                 FAR struct fb_planeinfo_s *pinfo);
/****************************************************************************
 * Private Functions
 ****************************************************************************/
static void lcd_lock(void)
{
  struct spi_dev_s *spi = g_spi_lcd_fb->spi;
  SPI_LOCK(spi, true);
  SPI_SETMODE(spi, SPIDEV_MODE0);
  SPI_SETBITS(spi, 8);
  SPI_HWFEATURES(spi, 0);
  SPI_SETFREQUENCY(spi, LCD_SPI_FREQ);
}

static void lcd_unlock(void)
{
  struct spi_dev_s *spi = g_spi_lcd_fb->spi;
  SPI_LOCK(spi, false);
}

/* GPIO : vendor/allwinnertech/chips/r528/drv/spi/drv_spi.c
 */
void spi_lcd_pin_init(void);
void spi_lcd_set_rst_pin(int val);
void spi_lcd_set_dc_pin(int val);
void spi_lcd_set_bl_pin(int val);

int msleep(unsigned int msecs);
static void SPI_LCD_Pin_Init(void)
{
  void spi_lcd_pin_init(void);
  spi_lcd_pin_init();
}

/* 复位SPI LCD */
static void LCD_Reset(void)
{
    //HAL_GPIO_WritePin(RESET_GPIO_Port,RESET_Pin,GPIO_PIN_RESET);
    spi_lcd_set_rst_pin(0);
    msleep(100);
    //HAL_GPIO_WritePin(RESET_GPIO_Port,RESET_Pin,GPIO_PIN_SET);
    spi_lcd_set_rst_pin(1);
    msleep(100);
}

/* 控制SPI LCD的背光, enable非零时表示打开背光,否则熄灭背光 */
static void LCD_BackLightControl(uint32_t enable)
{
    void spi_lcd_set_bl_pin(int val);
    if (enable)
    {
        //HAL_GPIO_WritePin(PWM_GPIO_Port,PWM_Pin,GPIO_PIN_SET);
        spi_lcd_set_bl_pin(1);
    }
    else
    {
        //HAL_GPIO_WritePin(PWM_GPIO_Port,PWM_Pin,GPIO_PIN_RESET);
        spi_lcd_set_bl_pin(0);
    }
}

/**********************************************************************
 * 函数名称： LCD_SetDataLine
 * 功能描述： 设置SPI LCD的D/C引脚为高电平,表示要传输的是数据
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/02/01        V1.0     韦东山       创建
 ***********************************************************************/
void LCD_SetDataLine(void)
{
    //HAL_GPIO_WritePin(GPIOD,RS_Pin,GPIO_PIN_SET);
    spi_lcd_set_dc_pin(1);
}

/* 设置SPI LCD的D/C引脚为低电平,表示要传输的是命令 */
static void LCD_SetCmdLine(void)
{
    //HAL_GPIO_WritePin(GPIOD,RS_Pin,GPIO_PIN_RESET);
    spi_lcd_set_dc_pin(0);
}

/* 给SPI LCD发送多个数据,调用此函数前应该先调用LCD_SetDataLine */
static int SPI_WriteDatas(uint8_t *TxData,uint16_t size)
{
    struct spi_dev_s *spi = g_spi_lcd_fb->spi;
    
    lcd_lock();
    SPI_SELECT(spi, 0, true); 
    SPI_SNDBLOCK(spi, TxData, size);  
    SPI_SELECT(spi, 0, false); 
    lcd_unlock();

    return 0;
}

/* 给SPI LCD发送1个命令 */
static int LCD_WriteCmd(uint8_t cmd)
{
    LCD_SetCmdLine();
    return SPI_WriteDatas(&cmd, 1);
}

/* 给SPI LCD发送1个参数,通常是先调用LCD_WriteCmd,再调用LCD_WritePara */
static int LCD_WritePara(uint8_t data)
{
    LCD_SetDataLine();
    return SPI_WriteDatas(&data, 1);
}

/**********************************************************************
 * 函数名称： LCD_WriteDatas
 * 功能描述： 给SPI LCD发送多个数据
 * 输入参数： data  - 数据buf
 *            count - 要发送的数据个数
 * 输出参数： 无
 * 返 回 值： 0 - 成功, (-1) - 错误, (-2) - 忙, (-3) - 超时
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/02/01        V1.0     韦东山       创建
 ***********************************************************************/
int LCD_WriteDatas(uint8_t *datas, uint32_t count)
{
    //HAL_GPIO_WritePin(GPIOD,RS_Pin,GPIO_PIN_SET);  /* 由调用者设置RS引脚 */
    return SPI_WriteDatas(datas, count);
}

/**********************************************************************
 * 函数名称： LCD_Init
 * 功能描述： 初始化LCD
 * 输入参数： rotation - 旋转角度, 取值如下
 *    LCD_DISPLAY_ROTATION_0,
 *    LCD_DISPLAY_ROTATION_90,
 *    LCD_DISPLAY_ROTATION_180,
 *    LCD_DISPLAY_ROTATION_270,
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/02/01        V1.0     韦东山       创建
 ***********************************************************************/
static void LCD_Init(void)
{       
    LCD_Reset();    
    LCD_BackLightControl(1);
    
#if 1   
    // Positive Gamma Control
    LCD_WriteCmd( 0xe0);
    LCD_WritePara(0xf0);
    LCD_WritePara(0x3e);
    LCD_WritePara(0x30);
    LCD_WritePara(0x06);
    LCD_WritePara(0x0a);
    LCD_WritePara(0x03);
    LCD_WritePara(0x4d);
    LCD_WritePara(0x56);
    LCD_WritePara(0x3a);
    LCD_WritePara(0x06);
    LCD_WritePara(0x0f);
    LCD_WritePara(0x04);
    LCD_WritePara(0x18);
    LCD_WritePara(0x13);
    LCD_WritePara(0x00);

    // Negative Gamma Control
    LCD_WriteCmd(0xe1);
    LCD_WritePara(0x0f);
    LCD_WritePara(0x37);
    LCD_WritePara(0x31);
    LCD_WritePara(0x0b);
    LCD_WritePara(0x0d);
    LCD_WritePara(0x06);
    LCD_WritePara(0x4d);
    LCD_WritePara(0x34);
    LCD_WritePara(0x38);
    LCD_WritePara(0x06);
    LCD_WritePara(0x11);
    LCD_WritePara(0x01);
    LCD_WritePara(0x18);
    LCD_WritePara(0x13);
    LCD_WritePara(0x00);
    
    // Power Control 1
    LCD_WriteCmd(0xc0);
    LCD_WritePara(0x18);
    LCD_WritePara(0x17);

    // Power Control 2
    LCD_WriteCmd(0xc1);
    LCD_WritePara(0x41);

    // Power Control 3
    LCD_WriteCmd(0xc5);
    LCD_WritePara(0x00);

    // VCOM Control
    LCD_WriteCmd(0x1a);
    LCD_WritePara(0x80);

    // Memory Access Control
    LCD_WriteCmd(0x36);
    LCD_WritePara(0x48);

    // Pixel Interface Format
    LCD_WriteCmd(0x3a);
    LCD_WritePara(0x55);

    // Interface Mode Control
    LCD_WriteCmd(0xb0);
    LCD_WritePara(0x00);

    // Frame Rate Control
    LCD_WriteCmd(0xb1);
    LCD_WritePara(0xa0);

    // Display Inversion Control
    LCD_WriteCmd(0xb4);
    LCD_WritePara(0x02);

    // Display Function Control
    LCD_WriteCmd(0xb6);
    LCD_WritePara(0x02);
    LCD_WritePara(0x02);

    // Set image function
    LCD_WriteCmd(0xe9);
    LCD_WritePara(0x00);

    //Adjust Control 3
    LCD_WriteCmd(0xf7);
    LCD_WritePara(0xa9);
    LCD_WritePara(0x51);
    LCD_WritePara(0x2c);
    LCD_WritePara(0x82);

    // Write_memory_start
    LCD_WriteCmd(0x21);
    msleep(120);
    //Exit Sleep
    LCD_WriteCmd(0x11);
    msleep(120);

    LCD_WriteCmd(0x36);
    LCD_WritePara(0x48);

    // switch (rotation)
    // {
    //     case LCD_DISPLAY_ROTATION_0:
    //         LCD_WriteCmd(0x36);
    //         LCD_WritePara(0x48);
    //         g_lcd_height = 480;
    //         g_lcd_width  = 320;
    //         break;
    //     case LCD_DISPLAY_ROTATION_90:
    //         LCD_WriteCmd(0x36);
    //         LCD_WritePara(0xe8);
    //         g_lcd_height = 320;
    //         g_lcd_width  = 480;
    //         break;
    //     case LCD_DISPLAY_ROTATION_180:
    //         LCD_WriteCmd(0x36);
    //         LCD_WritePara(0x88);
    //         g_lcd_height = 480;
    //         g_lcd_width  = 320;
    //         break;
    //     case LCD_DISPLAY_ROTATION_270:
    //         LCD_WriteCmd(0x36);
    //         LCD_WritePara(0x28);
    //         g_lcd_height = 320;
    //         g_lcd_width  = 480;
    //         break;
    //     default:
    //         LCD_WriteCmd(0x36);
    //         LCD_WritePara(0x48);
    //         g_lcd_height = 480;
    //         g_lcd_width  = 320;
    //         break;
    // }

    // set_screen_size
    LCD_WriteCmd(0x2a);
    LCD_WritePara(0x00);
    LCD_WritePara(0x00);
    LCD_WritePara(0x01);
    LCD_WritePara(0x3f);

    LCD_WriteCmd(0x2b);
    LCD_WritePara(0x00);
    LCD_WritePara(0x00);
    LCD_WritePara(0x01);
    LCD_WritePara(0xdf);

    //Display on
    LCD_WriteCmd(0x29);
    msleep(120);
#else
    LCD_WriteCmd(0x11);
    msleep(120);
    LCD_WriteCmd(0x36);     // Memory Data Access Control MY,MX~~
    LCD_WritePara(0x48);   

    LCD_WriteCmd(0x3A);     
    //LCDDrvWriteReg(0x55);
    LCD_WritePara(0x55);   //LCDDrvWriteDat(0x66);

    LCD_WriteCmd(0xF0);     // Command Set Control
    LCD_WritePara(0xC3);   

    LCD_WriteCmd(0xF0);     
    LCD_WritePara(0x96);   

    LCD_WriteCmd(0xB4);     
    LCD_WritePara(0x01);   

    LCD_WriteCmd(0xB7);     
    LCD_WritePara(0xC6);   

    LCD_WriteCmd(0xC0);     
    LCD_WritePara(0x80);   
    LCD_WritePara(0x45);   

    LCD_WriteCmd(0xC1);     
    LCD_WritePara(0x13);   //18  //00

    LCD_WriteCmd(0xC2);     
    LCD_WritePara(0xA7);   

    LCD_WriteCmd(0xC5);     
    LCD_WritePara(0x0A);   

    LCD_WriteCmd(0xE8);     
    LCD_WritePara(0x40);
    LCD_WritePara(0x8A);
    LCD_WritePara(0x00);
    LCD_WritePara(0x00);
    LCD_WritePara(0x29);
    LCD_WritePara(0x19);
    LCD_WritePara(0xA5);
    LCD_WritePara(0x33);

    LCD_WriteCmd(0xE0);
    LCD_WritePara(0xD0);
    LCD_WritePara(0x08);
    LCD_WritePara(0x0F);
    LCD_WritePara(0x06);
    LCD_WritePara(0x06);
    LCD_WritePara(0x33);
    LCD_WritePara(0x30);
    LCD_WritePara(0x33);
    LCD_WritePara(0x47);
    LCD_WritePara(0x17);
    LCD_WritePara(0x13);
    LCD_WritePara(0x13);
    LCD_WritePara(0x2B);
    LCD_WritePara(0x31);

    LCD_WriteCmd(0xE1);
    LCD_WritePara(0xD0);
    LCD_WritePara(0x0A);
    LCD_WritePara(0x11);
    LCD_WritePara(0x0B);
    LCD_WritePara(0x09);
    LCD_WritePara(0x07);
    LCD_WritePara(0x2F);
    LCD_WritePara(0x33);
    LCD_WritePara(0x47);
    LCD_WritePara(0x38);
    LCD_WritePara(0x15);
    LCD_WritePara(0x16);
    LCD_WritePara(0x2C);
    LCD_WritePara(0x32);
     

    LCD_WriteCmd(0xF0);     
    LCD_WritePara(0x3C);   

    LCD_WriteCmd(0xF0);     
    LCD_WritePara(0x69);   

    msleep(120);

    LCD_WriteCmd(0x21);     

    LCD_WriteCmd(0x29);     
#endif  

}

/**********************************************************************
 * 函数名称： LCD_SetWindows
 * 功能描述： 设置LCD接收数据的窗口,它会划定一个矩形对应的显存,后面写入的数据都对应这个区域
 * 输入参数： 无
 * 输出参数： (x1,y1) - 左上角坐标(此坐标包含在这个窗口里面)
 *            (x2,y2) - 右下角坐标(此坐标包含在这个窗口里面)
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/02/01        V1.0     韦东山       创建
 ***********************************************************************/
void LCD_SetWindows(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
    LCD_WriteCmd(0x2a);
    LCD_WritePara((x1 >> 8) & 0xFF);
    LCD_WritePara(x1 & 0xFF);
    LCD_WritePara((x2 >> 8) & 0xFF);
    LCD_WritePara(x2 & 0xFF);
    

    LCD_WriteCmd(0x2b);
    LCD_WritePara((y1 >> 8) & 0xFF);
    LCD_WritePara(y1 & 0xFF);
    LCD_WritePara((y2 >> 8) & 0xFF);
    LCD_WritePara(y2 & 0xFF);

    LCD_WriteCmd(0x2C);
}

static int spi_lcd_thread_func(int argc, char **argv)
{
	unsigned char *fb;
  int xres = g_spi_lcd_fb->videoinfo.xres;
  int yres = g_spi_lcd_fb->videoinfo.yres;
  struct spi_dev_s *spi = g_spi_lcd_fb->spi;
  
	while (1) 
	{
		fb  = g_spi_lcd_fb->planeinfo.fbmem;

		/* 1. 从Framebuffer得到数据 */
		/* 2. 转换格式(APP already) */
    /* 3. 通过SPI发送给OLED */

    LCD_SetWindows(0, 0, xres-1, yres-1);
		LCD_SetDataLine(); 
    lcd_lock();
    SPI_SELECT(spi, 0, true); 

    for (int y = 0; y < g_spi_lcd_fb->videoinfo.yres; y++)
    {
  	    SPI_SNDBLOCK(spi, fb, g_spi_lcd_fb->planeinfo.stride);
        fb += g_spi_lcd_fb->planeinfo.stride;
    }
    SPI_SELECT(spi, 0, false); 
    lcd_unlock();

		/* 4. 休眠一会 */
		usleep(1);
	}
	return 0;
}

/****************************************************************************
 * Name: spi_lcd_getvideoinfo
 ****************************************************************************/

static int spi_lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                 FAR struct fb_videoinfo_s *vinfo)
{
  FAR struct spi_lcd_fb_s *fb = (FAR struct spi_lcd_fb_s *)vtable;

  ginfo("vtable=%p vinfo=%p\n", vtable, vinfo);
  if (fb && vinfo)
    {
      memcpy(vinfo, &fb->videoinfo, sizeof(struct fb_videoinfo_s));
      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
}

/****************************************************************************
 * Name: spi_lcd_getplaneinfo
 ****************************************************************************/

static int spi_lcd_getplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                                 FAR struct fb_planeinfo_s *pinfo)
{
  FAR struct spi_lcd_fb_s *fb = (FAR struct spi_lcd_fb_s *)vtable;

  ginfo("vtable=%p planeno=%d pinfo=%p\n", vtable, planeno, pinfo);
  if (fb && planeno == 0 && pinfo)
    {
      memcpy(pinfo, &fb->planeinfo, sizeof(struct fb_planeinfo_s));
      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
}

static void sw_rgb565_swap(void * buf, uint32_t buf_size_px)
{
    uint32_t u32_cnt = buf_size_px / 2;
    uint16_t * buf16 = buf;
    uint32_t * buf32 = buf;

    while(u32_cnt >= 8) {
        buf32[0] = ((buf32[0] & 0xff00ff00) >> 8) | ((buf32[0] & 0x00ff00ff) << 8);
        buf32[1] = ((buf32[1] & 0xff00ff00) >> 8) | ((buf32[1] & 0x00ff00ff) << 8);
        buf32[2] = ((buf32[2] & 0xff00ff00) >> 8) | ((buf32[2] & 0x00ff00ff) << 8);
        buf32[3] = ((buf32[3] & 0xff00ff00) >> 8) | ((buf32[3] & 0x00ff00ff) << 8);
        buf32[4] = ((buf32[4] & 0xff00ff00) >> 8) | ((buf32[4] & 0x00ff00ff) << 8);
        buf32[5] = ((buf32[5] & 0xff00ff00) >> 8) | ((buf32[5] & 0x00ff00ff) << 8);
        buf32[6] = ((buf32[6] & 0xff00ff00) >> 8) | ((buf32[6] & 0x00ff00ff) << 8);
        buf32[7] = ((buf32[7] & 0xff00ff00) >> 8) | ((buf32[7] & 0x00ff00ff) << 8);
        buf32 += 8;
        u32_cnt -= 8;
    }

    while(u32_cnt) {
        *buf32 = ((*buf32 & 0xff00ff00) >> 8) | ((*buf32 & 0x00ff00ff) << 8);
        buf32++;
        u32_cnt--;
    }

    if(buf_size_px & 0x1) {
        uint32_t e = buf_size_px - 1;
        buf16[e] = ((buf16[e] & 0xff00) >> 8) | ((buf16[e] & 0x00ff) << 8);
    }

}
static int spi_lcd_updatearea(struct fb_vtable_s *vtable, FAR const struct fb_area_s *area)
{
  struct spi_dev_s *spi = g_spi_lcd_fb->spi;
  int stride = g_spi_lcd_fb->planeinfo.stride;
  int width_byte = area->w * (g_spi_lcd_fb->planeinfo.bpp >> 3);
  unsigned char *fb = g_spi_lcd_fb->planeinfo.fbmem + area->y * stride + area->x * (g_spi_lcd_fb->planeinfo.bpp >> 3);

  static uint8_t *swapped_datas;

  if (!swapped_datas)
  {
    swapped_datas = malloc(stride);
  }

  LCD_SetWindows(area->x, area->y, area->x + area->w - 1, area->y + area->h - 1);
  lcd_lock();
  LCD_SetDataLine();
  SPI_SELECT(spi, 0, true);
  for (int y = area->y; y < area->y + area->h; y++)
  {
    memcpy(swapped_datas, fb, width_byte);
    sw_rgb565_swap(swapped_datas, area->w);
    SPI_SNDBLOCK(spi, swapped_datas, width_byte);
    fb += stride;
  }
  SPI_SELECT(spi, 0, false);
  lcd_unlock();
  return 0;
}
/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: spi_lcd_fb_register
 ****************************************************************************/

int spi_lcd_fb_register(int display, struct spi_dev_s *spi)
{
  FAR struct spi_lcd_fb_s *fb;
  int ret = OK;

  fb = kmm_zalloc(sizeof(*fb));
  if (fb == NULL)
    {
      return -ENOMEM;
    }
  
  fb->spi = spi;

  fb->videoinfo.xres = 320;
  fb->videoinfo.yres = 480;
  fb->videoinfo.nplanes = 1;
  fb->videoinfo.fmt = FB_FMT_RGB16_565;

  fb->planeinfo.bpp = 16;
  fb->planeinfo.stride = fb->videoinfo.xres * (fb->planeinfo.bpp >> 3);
  fb->planeinfo.yres_virtual = fb->videoinfo.yres ;
  fb->planeinfo.xres_virtual = fb->videoinfo.xres;

  fb->planeinfo.fblen = fb->planeinfo.stride * fb->planeinfo.yres_virtual;
  fb->unalign_fb = kmm_zalloc(fb->planeinfo.fblen + 64); /* LVGL对Framebuffer有64字节对齐要求,多分配64字节以免越界 */
  fb->planeinfo.fbmem = (void *)(((uint32_t)fb->unalign_fb + 63) & ~63UL);
  syslog(LOG_DEBUG, "fb_register_device: unalign_fb = %p, fbmem=%p\n", fb->unalign_fb, fb->planeinfo.fbmem);
  if (fb->planeinfo.fbmem == NULL)
    {
      gerr("ERROR: Failed to allocate framebuffer memory: %zu KB\n",
           fb->planeinfo.fblen / 1024);
      ret = -ENOMEM;
      goto err_fbmem_alloc_failed;
    }

  fb->vtable.getplaneinfo = spi_lcd_getplaneinfo;
  fb->vtable.getvideoinfo = spi_lcd_getvideoinfo;
  fb->vtable.updatearea = spi_lcd_updatearea;

  ret = fb_register_device(display, 0, (FAR struct fb_vtable_s *)fb);
  if (ret < 0)
    {
      goto err_fb_register_failed;
    }

  g_spi_lcd_fb = fb;

  /* init spi lcd */
  SPI_LCD_Pin_Init();
  LCD_Init();

  /* create kthread : copy framebuffer to lcd */
  //kthread_create("spi_lcd_thread", SCHED_PRIORITY_DEFAULT, 8000, spi_lcd_thread_func, NULL);

  return OK;

err_fb_register_failed:
  kmm_free(fb->unalign_fb);
err_fbmem_alloc_failed:
  kmm_free(fb);
  return ret;
}
