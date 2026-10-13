// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "stdint.h"
#include "stdarg.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "fb_gfx.h"
#include "esp_log.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const char *TAG = "fb_gfx";



typedef struct { // Data stored PER GLYPH
        uint16_t bitmapOffset;     // Pointer into GFXfont->bitmap
        uint8_t  width, height;    // Bitmap dimensions in pixels
        uint8_t  xAdvance;         // Distance to advance cursor (x axis)
        int8_t   xOffset, yOffset; // Dist from cursor pos to UL corner
} GFXglyph;

typedef struct { // Data stored for FONT AS A WHOLE:
        uint8_t  *bitmap;      // Glyph bitmaps, concatenated
        GFXglyph *glyph;       // Glyph array
        uint8_t   first, last; // ASCII extents
        uint8_t   yAdvance;    // Newline distance (y axis)
        uint8_t   yOffset;     // Y offset of the font zero line (y axis)
} GFXfont;

#include "FreeMonoBold12pt7b.h"//14x24
#define gfxFont ((GFXfont*)(&FreeMonoBold12pt7b))

void fb_gfx_draw_point(fb_data_t *fb, int32_t x, int32_t y, uint32_t color)
{
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height) {
        return;
    }
    uint8_t *data = fb->data + ((x + (y * fb->width)) * fb->bytes_per_pixel);
    data[0] = color >> 8;
    data[1] = color;
}

void fb_gfx_draw_line(fb_data_t *fb, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color)
{
    double a = 0.0,b = 0.0;         // 解析式: y=ax+b
    int32_t sx = MIN(x1, x2) ;           // sx 与 ex 需要成对使用 y与x不能混用
    int32_t sy = MIN(y1, y2) ;
    int32_t ex = MAX(x1, x2) ;
    int32_t ey = MAX(y1, y2) ;
    if (x1 == x2)
    {
        //垂直线
        for(int32_t i = sy;i <= ey;i++)
            fb_gfx_draw_point(fb, x1, i, color);
    }
    else if (y1 == y2)
    {
        //水平线
        for(int32_t i = sx;i <= ex;i++)
            fb_gfx_draw_point(fb, i, y1, color);
    }
    else if((ex-sx) < (ey-sy))      //使用最大最小值判断确保 Δ值不为负数
    {
        //使用y方向做扫描
        a = (x1 - x2) * 1.0/(y1 - y2);//y方向的斜率为 Δx/Δy
        b = x1 - a * y1;
        for(int32_t i = sy;i <= ey;i++)
        {
            int32_t x = (int32_t)(a*i+b);
            // 由于浮点数计算误差，可能略微超出范围，需要边界检查
            if (x >= sx && x <= ex) {
                fb_gfx_draw_point(fb, x, i, color);
            }
        }
    }
    else
    {
        //使用x方向做扫描
        a = (y1 - y2) * 1.0/ (x1 - x2);//y方向的斜率为 Δx/Δy
        b = y1 - a * x1;
        for(int32_t i = sx;i <= ex;i++)  // 修复：应该使用 sx 到 ex，而不是 sy 到 ey
        {
            int32_t y = (int32_t)(a*i+b);
            // 由于浮点数计算误差，可能略微超出范围，需要边界检查
            if (y >= sy && y <= ey) {
                fb_gfx_draw_point(fb, i, y, color);
            }
        }
    }
}
 

void fb_gfx_fillRect(fb_data_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    int32_t line_step = (fb->width - w) * fb->bytes_per_pixel;
    uint8_t *data = fb->data + ((x + (y * fb->width)) * fb->bytes_per_pixel);
    uint8_t c0 = color >> 16;
    uint8_t c1 = color >> 8;
    uint8_t c2 = color;
    for (int i=0; i<h; i++){
        for (int j=0; j<w; j++){
            data[0] = c1;
            data[1] = c2;
            data+=2;
        }
        data += line_step;
    }
}

void fb_gfx_drawFastHLine(fb_data_t *fb, int32_t x, int32_t y, int32_t w, uint32_t color)
{
    fb_gfx_fillRect(fb, x, y, w, 1, color);

    uint8_t *data = fb->data + ((x + (y * fb->width)) * fb->bytes_per_pixel);
    uint8_t c1 = color >> 8;
    uint8_t c2 = color;
    for (int j=0; j<w; j++){
        data[0] = c1;
        data[1] = c2;
        data+=2;
    }
 
}

void fb_gfx_drawFastVLine(fb_data_t *fb, int32_t x, int32_t y, int32_t h, uint32_t color)
{
    fb_gfx_fillRect(fb, x, y, 1, h, color);
}

void fb_gfx_draw_circle(fb_data_t *fb, int32_t cx, int32_t cy, int32_t radius, uint32_t color)
{
    if (radius <= 0) {
        return;
    }
    int32_t x = radius;
    int32_t y = 0;
    int32_t err = 1 - radius;

    while (x >= y) {
        fb_gfx_draw_point(fb, cx + x, cy + y, color);
        fb_gfx_draw_point(fb, cx + y, cy + x, color);
        fb_gfx_draw_point(fb, cx - y, cy + x, color);
        fb_gfx_draw_point(fb, cx - x, cy + y, color);
        fb_gfx_draw_point(fb, cx - x, cy - y, color);
        fb_gfx_draw_point(fb, cx - y, cy - x, color);
        fb_gfx_draw_point(fb, cx + y, cy - x, color);
        fb_gfx_draw_point(fb, cx + x, cy - y, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

uint8_t fb_gfx_putc(fb_data_t *fb, int32_t x, int32_t y, uint32_t color, unsigned char c)
{
    uint16_t line_width;
    uint8_t  xa = 0, bit = 0, bits = 0, xx, yy;
    uint8_t  *bitmap;
    GFXglyph *glyph;

    if ((c < 32) || (c < gfxFont->first) || (c > gfxFont->last)) {
        return xa;
    }

    c -= gfxFont->first;

    glyph  = &(gfxFont->glyph[c]);
    bitmap = gfxFont->bitmap + glyph->bitmapOffset;

    xa = glyph->xAdvance;
    x += glyph->xOffset;
    y += glyph->yOffset;
    y += gfxFont->yOffset;
    line_width = 0;

    for(yy=0; yy<glyph->height; yy++) {
        for(xx=0; xx<glyph->width; xx++) {
            if(bit == 0) {
                bits = *bitmap++;
                bit  = 0x80;
            }
            if(bits & bit) {
                line_width++;
            } else if (line_width) {
                fb_gfx_drawFastHLine(fb, x+xx-line_width, y+yy, line_width, color);
                line_width=0;
            }
            bit >>= 1;
        }
        if (line_width) {
            fb_gfx_drawFastHLine(fb, x+xx-line_width, y+yy, line_width, color);
            line_width=0;
        }
    }
    return xa;
}

uint32_t fb_gfx_print(fb_data_t *fb, int32_t x, int32_t y, uint32_t color, const char * str)
{
    uint32_t l = 0;
    int xc = x, yc = y, lc = fb->width - gfxFont->glyph[0].xAdvance;
    uint8_t  fh = gfxFont->yAdvance;
    char c = *str++;
    while(c){
        if(c != '\r'){
            if(c == '\n'){
                yc += fh;
                xc = x;
            } else {
                if(xc > lc){
                    yc += fh;
                    xc = x;
                }
                xc += fb_gfx_putc(fb, xc, yc, color, c);
            }
        }
        l++;
        c = *str++;
    }
    return l;
}

uint32_t fb_gfx_printf(fb_data_t *fb, int32_t x, int32_t y, uint32_t color, const char *format, ...)
{
    char loc_buf[64];
    char * temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(copy);
    if(len >= sizeof(loc_buf)){
        temp = (char*)malloc(len+1);
        if(temp == NULL) {
            return 0;
        }
    }
    vsnprintf(temp, len+1, format, arg);
    va_end(arg);
    fb_gfx_print(fb, x, y, color, temp);
    if(len > 64){
        free(temp);
    }
    return len;
}
