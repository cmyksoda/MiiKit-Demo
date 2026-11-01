#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	sdk-mii-demo
BUILD		:=	build
SOURCES		:=	source source/mii source/libwiiinput
DATA		:=	data gen
INCLUDES	:=	source
FONT		?=	$(HOME)/.local/share/fonts/f/FOT_RodinNTLG_Pro_DB.otf

ifneq ($(strip $(TOUR)),)
TARGET		:=	sdk-mii-demo-tour
BUILD		:=	build-tour
endif

CFLAGS	= -g -O2 -Wall -Wextra $(MACHDEP) $(INCLUDE) $(if $(TOUR),-DTOUR) $(if $(TOUR_GRID),-DTOUR_GRID) $(if $(TOUR_DETAIL),-DTOUR_DETAIL)
CXXFLAGS	=	$(CFLAGS)

LDFLAGS	=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS	:=	$(if $(TOUR),-lfat) -lwiiuse -lbte -lasnd -lmad -logc -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=

# Extra controllers, folded onto the Wii Remote / GameCube pad calls this program already makes (see source/libwiiinput/extrainput.h):
# USB mouse, pen tablets, uDraw GameTablet and the GameCube adapter for Wii U always; DualShock 3, Wii U Pro Controller
# and Wii U GamePad once scripts/build_controller_portlibs.sh has built their libraries into ./portlibs/ppc
export TOPDIR ?= $(CURDIR)
EXTRA_PORTLIBS	:=	$(TOPDIR)/portlibs/ppc
CFLAGS		+=	-DHAVE_TABLET -DHAVE_GCADAPTER -I$(TOPDIR)/source/libwiiinput -include extrainput_compat.h
LDFLAGS		+=	-Wl,--wrap=classic_ctrl_handshake,--wrap=parse_event,--wrap=USB_GetDeviceList
ifneq ($(wildcard $(EXTRA_PORTLIBS)/lib/libsicksaxis.a),)
CFLAGS		+=	-DHAVE_SICKSAXIS
LIBS		:=	-lsicksaxis $(LIBS)
endif
ifneq ($(wildcard $(EXTRA_PORTLIBS)/lib/libwupc.a),)
CFLAGS		+=	-DHAVE_WUPC
LIBS		:=	-lwupc $(LIBS)
LDFLAGS		+=	-Wl,-wrap,wiiuse_accept
endif
ifneq ($(wildcard $(EXTRA_PORTLIBS)/lib/libwiidrc.a),)
CFLAGS		+=	-DHAVE_WIIDRC
LIBS		:=	-lwiidrc $(LIBS)
endif
LIBDIRS		+=	$(EXTRA_PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(sort $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*))) rodin20.bin rodin32.bin cursor.bin)

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES), -iquote $(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)

export LIBPATHS	:= -L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------------
$(BUILD): gen/rodin20.bin gen/rodin32.bin gen/cursor.bin
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
gen/cursor.bin: tools/make_cursor.py assets/cursor/generic_point.png assets/cursor/shadow_point.png
	@mkdir -p gen
	python3 tools/make_cursor.py $@

gen/rodin%.bin: tools/make_font.py
	@mkdir -p gen
	python3 tools/make_font.py "$(FONT)" $* $@

clean:
	@echo clean ...
	@rm -fr build build-tour gen sdk-mii-demo*.elf sdk-mii-demo*.dol *.map

#---------------------------------------------------------------------------------
run:
	wiiload $(TARGET).dol


#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

#---------------------------------------------------------------------------------
# The font pages and the music, linked in
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	$(bin2o)

%.mp3.o	%_mp3.h :	%.mp3
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
