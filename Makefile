#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPRO)/libogc2/gamecube_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#
# vgmstream is built straight into the program (no separate library step).
# Only the parsers named in vgm_formats.txt go into its format table; the
# linker's --gc-sections then drops every parser nothing refers to.
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
VGMSTREAM	:=	vgmstream/src
SOURCES		:=	source $(VGMSTREAM) $(VGMSTREAM)/base $(VGMSTREAM)/coding \
				$(VGMSTREAM)/coding/libs $(VGMSTREAM)/layout $(VGMSTREAM)/meta $(VGMSTREAM)/util
DATA		:=	data
TEXTURES	:=	textures
INCLUDES	:=	source $(VGMSTREAM)

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

CFLAGS		= -g -O2 -Wall -Wno-format-truncation -ffunction-sections -fdata-sections $(MACHDEP) $(INCLUDE) \
				-D'GITREVISION="$(shell git rev-parse --short HEAD)"' -D'GITVERSION="$(shell git rev-list HEAD | wc -l)"'
CXXFLAGS	= $(CFLAGS)

LDFLAGS		= -g $(MACHDEP) -Wl,--gc-sections -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
# libjansson: pacman -S ppc-jansson, FreeType: pacman -S ppc-freetype (libogc2-devkitpro repo)
LIBS	:=	-ljansson -lfreetype -lpng16 -lbz2 -lbrotlidec -lbrotlicommon -lz -lfat -lansnd -logc -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(foreach dir,$(TEXTURES),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
# (vgmstream_init.c is replaced by the filtered copy generated in the build dir)
#---------------------------------------------------------------------------------
CFILES		:=	$(filter-out vgmstream_init.c,$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))) \
				vgmstream_init_gc.c
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
SCFFILES	:=	$(foreach dir,$(TEXTURES),$(notdir $(wildcard $(dir)/*.scf)))
TPLFILES	:=	$(SCFFILES:.scf=.tpl)

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) $(addsuffix .o,$(TPLFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)
# vgmstream's objects, to build them without its (harmless) unused-variable noise
export VGM_OFILES	:=	$(patsubst %.c,%.o,$(filter-out vgmstream_init.c,$(notdir $(wildcard $(VGMSTREAM)/*.c $(VGMSTREAM)/*/*.c $(VGMSTREAM)/*/*/*.c)))) \
						vgmstream_init_gc.o
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES))) $(addsuffix .h,$(subst .,_,$(TPLFILES)))

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include/freetype2) \
			-I$(CURDIR)/$(BUILD) \
			-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
			-L$(LIBOGC_LIB)

.PHONY: $(BUILD) clean run

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

#---------------------------------------------------------------------------------
run:
	usb-load $(OUTPUT).dol

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC) : $(HFILES)

$(VGM_OFILES) : CFLAGS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
						-Wno-unused-const-variable -Wno-format-truncation -Wno-char-subscripts

#---------------------------------------------------------------------------------
# vgmstream's format table, cut down to the parsers listed in vgm_formats.txt
# (blank lines and # comments ignored). Everything else in upstream's
# vgmstream_init.c is kept as is.
#---------------------------------------------------------------------------------
vgmstream_init_gc.c : $(TOPDIR)/$(VGMSTREAM)/vgmstream_init.c $(TOPDIR)/vgm_formats.txt
	@echo generating $@ from vgm_formats.txt
	@awk 'FNR==NR { sub(/#.*/, ""); gsub(/[ \t\r]/, ""); if ($$0 != "") keep["init_vgmstream_" $$0] = 1; next } \
		match($$0, /^[ \t]*init_vgmstream_[A-Za-z0-9_]+,/) { \
			name = substr($$0, RSTART, RLENGTH - 1); sub(/^[ \t]*/, "", name); \
			if (!(name in keep)) next } \
		{ print }' $(TOPDIR)/vgm_formats.txt $< > $@

#---------------------------------------------------------------------------------
%.jpg.o	%_jpg.h :	%.jpg
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.ttf.o	%_ttf.h :	%.ttf
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.tpl.o	%_tpl.h :	%.tpl
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
