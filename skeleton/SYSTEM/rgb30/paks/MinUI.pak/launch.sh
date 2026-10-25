#!/bin/sh

export PLATFORM="rgb30"
export SDCARD_PATH="/storage/roms"
export BIOS_PATH="$SDCARD_PATH/Bios"
export SAVES_PATH="$SDCARD_PATH/Saves"
export SYSTEM_PATH="$SDCARD_PATH/.system/$PLATFORM"
export CORES_PATH="$SYSTEM_PATH/cores"
export USERDATA_PATH="$SDCARD_PATH/.userdata/$PLATFORM"
export SHARED_USERDATA_PATH="$SDCARD_PATH/.userdata/shared"
export LOGS_PATH="$USERDATA_PATH/logs"
export DATETIME_PATH="$SHARED_USERDATA_PATH/datetime.txt"

#######################################

# Moss applies its timezone setting while it starts EmulationStation, which this boot skips,
# so without this the clock (and the Clock tool) would be in UTC
TZNAME=$(. /etc/profile >/dev/null 2>&1; get_setting system.timezone)
if [ -n "$TZNAME" ] && [ -f "/usr/share/zoneinfo/$TZNAME" ]; then
	ln -sf "/usr/share/zoneinfo/$TZNAME" /var/run/localtime
fi

# JELOS aggressively repairs disks and leaves litter on the SD card
rm $SDCARD_PATH/FSCK*.REC

export PATH=$SYSTEM_PATH/bin:$PATH
export LD_LIBRARY_PATH=$SYSTEM_PATH/lib:$LD_LIBRARY_PATH

# required for sdl12-compat
export SDL_VIDEODRIVER=kmsdrm
export SDL_AUDIODRIVER=alsa

# required for sdl1 fbcon
# export SDL_VIDEODRIVER=fbcon
# export SDL_AUDIODRIVER=alsa
# export SDL_NOMOUSE=1

echo userspace > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
CPU_PATH=/sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed
CPU_SPEED_PERF=1992000
echo $CPU_SPEED_PERF > $CPU_PATH

#######################################

keymon.elf & # &> $SDCARD_PATH/keymon.txt &

#######################################

mkdir -p "$LOGS_PATH"
mkdir -p "$SHARED_USERDATA_PATH/.minui"
AUTO_PATH=$USERDATA_PATH/auto.sh
if [ -f "$AUTO_PATH" ]; then
	"$AUTO_PATH" # &> $LOGS_PATH/auto.txt
fi

cd $(dirname "$0")

#######################################

# new games get a title picture (Tools > Settings > "Art for new games": Automatic): once per boot,
# no screen, a few seconds at most
TITLEART=$SDCARD_PATH/Tools/$PLATFORM/TitleArt.pak/titleart.elf
if [ "$(cat $USERDATA_PATH/titleart-auto 2>/dev/null)" = "1" ] && [ -x "$TITLEART" ]; then
	# the tool keeps to a few seconds itself; the limit is for the case where it never comes back (nothing may hold up a boot)
	if command -v timeout >/dev/null 2>&1; then timeout 15 "$TITLEART" --auto > "$LOGS_PATH/titleart.txt" 2>&1
	else "$TITLEART" --auto > "$LOGS_PATH/titleart.txt" 2>&1; fi
fi

EXEC_PATH=/tmp/minui_exec
NEXT_PATH="/tmp/next"
touch "$EXEC_PATH" && sync
while [ -f "$EXEC_PATH" ]; do
	echo $CPU_SPEED_PERF > $CPU_PATH
	minui.elf &> $LOGS_PATH/minui.txt
	echo `date +'%F %T'` > "$DATETIME_PATH"
	sync
	
	if [ -f $NEXT_PATH ]; then
		CMD=`cat $NEXT_PATH`
		eval $CMD
		rm -f $NEXT_PATH
		echo $CPU_SPEED_PERF > $CPU_PATH
		echo `date +'%F %T'` > "$DATETIME_PATH"
		sync
	fi
done

shutdown # just in case
