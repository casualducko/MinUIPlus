# workspace (all)

###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

###########################################################

.PHONY: all

all:
	cd ./$(PLATFORM)/libmsettings && make
	cd ./$(PLATFORM) && make early # eg. other libs
	cd ./$(PLATFORM)/keymon && make
	cd ./all/minui/ && make
	cd ./all/minarch/ && make
	cd ./all/clock/ && make
	cd ./all/minput/ && make
	cd ./all/romulus/ && make
	cd ./all/pico/ && make
	cd ./all/power/ && make
	cd ./all/stats/ && make
	cd ./all/controls/ && make
	cd ./all/theme/ && make
	cd ./all/titleart/ && make
	cd ./all/info/ && make
	cd ./all/update/ && make
	cd ./all/gallery/ && make
	cd ./all/switcher/ && make
	cd ./all/wifi/ && make
	cd ./all/syncsettings/ && make
	cd ./all/say/ && make
	cd ./$(PLATFORM)/cores && make
	cd ./$(PLATFORM) && make

clean:
	cd ./$(PLATFORM)/libmsettings && make clean
	cd ./$(PLATFORM)/keymon && make clean
	cd ./all/minui/ && make clean
	cd ./all/minarch/ && make clean
	cd ./all/clock/ && make clean
	cd ./all/minput/ && make clean
	cd ./all/romulus/ && make clean
	cd ./all/pico/ && make clean
	cd ./all/power/ && make clean
	cd ./all/stats/ && make clean
	cd ./all/controls/ && make clean
	cd ./all/theme/ && make clean
	cd ./all/titleart/ && make clean
	cd ./all/info/ && make clean
	cd ./all/update/ && make clean
	cd ./all/gallery/ && make clean
	cd ./all/switcher/ && make clean
	cd ./all/wifi/ && make clean
	cd ./all/syncsettings/ && make clean
	cd ./all/say/ && make clean
	cd ./$(PLATFORM) && make clean