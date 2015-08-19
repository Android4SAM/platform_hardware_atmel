ifneq ($(filter sama5d3-ek, $(TARGET_BOOTLOADER_BOARD_NAME)),)
include $(call all-named-subdir-makefiles,sama5dx)
endif
ifneq ($(filter sama5d4-ek, $(TARGET_BOOTLOADER_BOARD_NAME)),)
include $(call all-named-subdir-makefiles,sama5dx)
endif
ifneq ($(filter sama5d2-ek, $(TARGET_BOOTLOADER_BOARD_NAME)),)
include $(call all-named-subdir-makefiles,sama5dx)
endif
