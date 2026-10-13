# ESP-IDF 6 renamed the libc component; IDF 5 retains the newlib name.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(FB_GFX_LIBC_COMPONENT esp_libc)
else()
    set(FB_GFX_LIBC_COMPONENT newlib)
endif()

idf_component_register(
    SRCS "fb_gfx.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${FB_GFX_LIBC_COMPONENT}
)
