#!/system/bin/sh
echo "###############in hdmi2usb sh"
if [ $# -ne 1 ]; then
echo "param error: $0 0/1"
return 0;
fi

card=`getprop wmt.usb.audio.card`
echo "usb audio card: ${card}"

if [ $1 -eq 1 ]; then
echo "switch to usb!"
if [ -c "/dev/snd/pcmC${card}D0p" ]; then
echo "#####have usb audio card ${card}"
/system/bin/am broadcast -a android.intent.action.USB_AUDIO_DEVICE_PLUG --ei state 1 --ei card ${card} --ei device 0
return 0
fi
fi

if [ $1 -eq 0 ]; then
echo "switch to hdmi!"
/system/bin/am broadcast -a android.intent.action.USB_AUDIO_DEVICE_PLUG --ei state 0
return 0
fi
