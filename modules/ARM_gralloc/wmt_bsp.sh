#!/bin/bash

if [ ! -d $BSP_LOCAL_PATH ]; then
    echo "Can not find BSP_LOCAL_PATH:$BSP_LOCAL_PATH folder.Abort!"
    return 1
fi

cp $PRODUCT_OUT/system/lib/hw/gralloc.default.so $BSP_LOCAL_PATH
cp $PRODUCT_OUT/obj/SHARED_LIBRARIES/gralloc.default_intermediates/export_includes $BSP_LOCAL_PATH

cp $PRODUCT_OUT/system/lib/hw/gralloc.wmt.so $BSP_LOCAL_PATH
cp $PRODUCT_OUT/obj/SHARED_LIBRARIES/gralloc.wmt_intermediates/export_includes $BSP_LOCAL_PATH

cp $LOCAL_PATH/Android.mk.bsp $BSP_LOCAL_PATH/Android.mk
