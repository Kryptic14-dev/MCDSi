# SPDX-License-Identifier: CC0-1.0
#
# Minecraft: Nintendo DS Edition - proyecto BlocksDS (solo ARM9)

BLOCKSDS	?= /opt/blocksds/core
BLOCKSDSEXT	?= /opt/blocksds/external

# Configuracion
NAME		:= Minecraft_DSi

GAME_TITLE	:= Minecraft DSi
GAME_SUBTITLE	:= Nintendo DSi Edition 1.7.10 
GAME_AUTHOR	:= MartusTheMartian_&_TholyThorch
GAME_ICON	:= icon.png

# Codigo y datos
SOURCEDIRS	:= source
INCLUDEDIRS	:= include
BINDIRS		:= data

# Librerias
LIBS		:= -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/libnds

# El nombre de la carpeta cambia entre versiones de BlocksDS: se buscan varias
DEFMK := $(firstword $(wildcard \
    $(BLOCKSDS)/sys/default_makefiles/rom_arm9only/Makefile \
    $(BLOCKSDS)/sys/default_makefiles/rom_arm9_only/Makefile \
    $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile))
ifeq ($(DEFMK),)
$(error No encuentro el Makefile base. Mira: ls $(BLOCKSDS)/sys/default_makefiles)
endif
include $(DEFMK)
