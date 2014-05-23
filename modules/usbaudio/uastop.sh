#!/system/bin/sh

echo "#######in uastop"
/system/bin/am broadcast -a android.intent.action.USB_AUDIO_DEVICE_PLUG --ei state 0
setprop wmt.usb.audio.card 0
