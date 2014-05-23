hardware_modules := ARM_gralloc gralloc hwcomposer audio local_time power usbaudio audio_remote_submix  wmt_gps
include $(call all-named-subdir-makefiles,$(hardware_modules))
