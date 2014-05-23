#!/system/bin/sh
echo "###############in uaplayback sh"
if [ -c "/dev/snd/pcmC1D0p" ]; then
setprop wmt.usb.audio.card 1
echo "#####have usb audio card 1"
/system/bin/am broadcast -a android.intent.action.USB_AUDIO_DEVICE_PLUG --ei state 1 --ei card 1 --ei device 0

return 0
fi
if [ -c "/dev/snd/pcmC2D0p" ]; then
setprop wmt.usb.audio.card 2
echo "###have usb audio card 2"
/system/bin/am broadcast -a android.intent.action.USB_AUDIO_DEVICE_PLUG --ei state 1 --ei card 2 --ei device 0       

return 0
fi
echo "##### no usb audio card found!!"
