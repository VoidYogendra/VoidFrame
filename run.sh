#!/system/bin/sh

# Stop the framework from fighting us during state transition
setprop sys.usb.config none
sleep 1

# Request the system framework to natively enable RNDIS and ADB
setprop sys.usb.config rndis,adb

# Wait up to 10 seconds for the kernel network device mapper to create the interface
for i in $(seq 1 10); do
    if [ -d /sys/class/net/rndis0 ] || [ -d /sys/class/net/usb0 ]; then
        break
    fi
    sleep 1
done

# Assign the IP configuration to whichever node the kernel created
sleep 1
ifconfig usb0 192.168.42.2 netmask 255.255.255.0 up 2>/dev/null
sleep 1
ifconfig rndis0 192.168.42.2 netmask 255.255.255.0 up 2>/dev/null
