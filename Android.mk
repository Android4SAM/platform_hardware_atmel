ifneq ($(filter sama5d3-ek, $(TARGET_BOOTLOADER_BOARD_NAME)),)
include $(call all-named-subdir-makefiles,sama5dx)
endif
