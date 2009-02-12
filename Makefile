KERNEL_NAME       := Quoppa
KERNEL_MAJOR_VER  := 0
KERNEL_MIDDLE_VER := 0
KERNEL_MINOR_VER  := 1

ifndef $(BUILD_ROOT)
	BUILD_ROOT := $(CURDIR)
endif

KERNELVERSION := $(KERNEL_MAJOR_VER).$(KERNEL_MIDDLE_VER).$(KERNEL_MINOR_VER)
VERFILE := include/version.h
ODIR := vmjari_objs
OBJECTS :=
CC := $(TOOLCHAIN)gcc
AR := $(TOOLCHAIN)ar
AS := $(TOOLCHAIN)as
LD := $(TOOLCHAIN)ld
OBJDUMP := $(TOOLCHAIN)objdump
OBJCOPY := $(TOOLCHAIN)objcopy

HOSTCC := gcc
HOSTLD := ld
# HOSTCFLAGS
# HOSTLDFLAGS

GREP := grep
GMAP := eza/gmap.py
LN := ln
RM := rm
CP := cp
MKDIR := mkdir
ECHO := /bin/echo

ifeq ($(VERBOSE),y)
Q := 
MAKE := make
else
Q := @
MAKE := make -s
endif


ifneq ($(NOCOLOR), y)
NOCOLOR :=
endif

CFLAGS = -Wall -nostdlib -nostdinc -fno-builtin -fomit-frame-pointer -g $(USER_CFLAGS)

LDFLAGS += -M
INCLUDE += -Iinclude

export CC LD AR OBJDUMP OBJCOPY GMAP GREP CPP AS ECHO
export HOSTCC HOSTLD HOSTCFLAGS HOSTLDFLAGS
export GREP MAKE LN RM GMAP MKDIR CP
export CFLAGS LDFLAGS INCLUDE
export BUILD_ROOT ARCH NOCOLOR OBJECTS
export KERNELVERSION

include include/Makefile.inc

-include .config
-include eza/arch/$(ARCH)/Makefile.inc

GENERICS = eza mm mlibc ipc server ds
ifeq ($(CONFIG_TEST),y)
GENERICS += tests
endif

.PHONY: all vmuielf rmap.bin collect_objects
all: host vmuielf bootimage

host:
	$(call echo-header,"kbuild")
	$(Q)$(MAKE) all -C kbuild BUILD_ROOT=$(BUILD_ROOT)

vmuielf: prepare $(addprefix generic_, $(GENERICS)) muielf
	$(call echo-label,"OBJCOPY","$@")
	$(Q)$(OBJCOPY) -O binary muielf $@

check_config:
ifeq ($(shell [ -f $(BUILD_ROOT)/.config ] && echo "ok"),)
	$(Q)$(MAKE) help_config
endif

prepare: check_config $(VERFILE)
	$(Q)$(call create_symlinks)
	$(Q)$(MKDIR) -p $(ODIR)

muielf: $(addprefix $(ODIR)/, kernel.ld rmap.o)	
	$(call echo-action,"LD","$^")
	$(Q)$(LD) -T $(ODIR)/kernel.ld $(LDFLAGS) $(OBJECTS) $(ODIR)/rmap.o -o $@ -Map $(ODIR)/muielf.map

generic_%:
	$(call echo-header,"$(subst generic_,,$@)")
	$(Q)$(MAKE) -f rules.mak target=$(subst generic_,,$@)

clean_%:
	$(call echo-header,"Cleaning $(subst clean_,,$@):")
	$(Q)$(MAKE) -f rules.mak target=$(subst clean_,,$@) clean

clean:
	$(Q)$(RM) -rf $(ODIR)
	$(Q)$(RM) -f muielf vmuielf boot.img
	$(Q)$(MAKE) -C. $(addprefix clean_, $(GENERICS))

clean_host:
	$(call echo-header,"Cleaning host")
	$(Q)$(MAKE) -C kbuild clean

cleanconf:
	$(call echo-header,"Cleaning configs")
	$(Q)$(RM) -rf .config include/autoconf.h include/config/* $(VERFILE)

distclean: clean_host clean cleanconf

$(ODIR)/rmap.o: $(ODIR)/rmap.bin
	$(call create_rmap)

$(ODIR)/kernel.ld: $(BUILD_ROOT)/eza/arch/$(ARCH)/kernel.ld.S
	$(call echo-action,"CPP","$<")
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -D__ASM__ -E -x c $< | $(GREP) -v "^\#" > $@

collect_objects:
	$(eval OBJECTS := $(call collect_objects))

$(ODIR)/rmap.bin: collect_objects $(ODIR)/kernel.ld	
	$(call pre_linking_action)	
	$(call echo-action,"LD","Linking all together...")
	$(Q)$(LD) -T $(ODIR)/kernel.ld $(LDFLAGS) -q $(OBJECTS) -o $@ -Map $(ODIR)/mui.pre.map	
	$(Q)$(OBJDUMP) -t $(OBJECTS) > $(ODIR)/mui.objdump
	$(Q)$(GMAP) $(addprefix $(ODIR)/, mui.pre.map mui.objdump rmap.bin)
	$(call echo-action,"MK","Regeneration...")
	$(call post_linking_action)
	$(Q)$(LD) -T $(ODIR)/kernel.ld $(LDFLAGS) $(OBJECTS) $(ODIR)/emapo.o -o $@ -Map $(ODIR)/mui.pre.map
	$(Q)$(OBJDUMP) -t $(OBJECTS) > $(ODIR)/mui.objdump	
	$(Q)$(GMAP) $(addprefix $(ODIR)/, mui.pre.map mui.objdump rmap.bin)

bootimage: vmuielf
ifneq ($(NOBUILDIMG),y) 
# Creates a 20 MB bootable FAT HD image: 44 tracks, 16 heads, 63 sectors
	@dd if=/dev/zero of=boot.img count=44352 bs=512
	@echo "drive c: file=\"`pwd`/boot.img\" partition=1" > ~/.mtoolsrc
	@mpartition -I -s 63 -t 44 -h 16 c:
	@mpartition -cpv -s 63 -t 44 -h 16 c:
	@mformat c:
	@mmd c:/boot
	@mmd c:/boot/grub
	@mcopy /boot/grub/stage1 c:/boot/grub
	@mcopy /boot/grub/stage2 c:/boot/grub
	@mcopy /boot/grub/fat_stage1_5 c:/boot/grub
	@echo "(hd0) boot.img" > bmap
	@printf "geometry (hd0) 44 16 63\nroot (hd0,0)\nsetup (hd0)\n" | /usr/sbin/grub --batch --device-map=bmap
	@printf "default 0\ntimeout=2\ntitle=MString kernel\nkernel=/boot/grub/kernel.bin" > ./menu.lst
	@mcopy ./menu.lst c:/boot/grub
	@mcopy ./vmuielf c:/boot/grub/kernel.bin
	@rm -f ./menu.lst ~/.mtoolsrc bmap
	@echo
	@echo "*********************************************************************************"
	@echo "            Bootable HD image 'boot.img' was successfilly created."
	@echo "       Make sure the following lines are present in your 'bochsrc' file:"
	@echo " ata0-master: type=disk, path=boot.img, mode=flat, cylinders=44, heads=16, spt=63"
	@echo " boot: disk"
	@echo "*********************************************************************************"
endif

$(VERFILE):	
	$(Q)$(ECHO) "#ifndef __VERSION_H__" > $(VERFILE)
	$(Q)$(ECHO) "#define __VERSION_H__" >> $(VERFILE)
	$(Q)$(ECHO) >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_VERSION $(KERNEL_MAJOR_VER)" >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_SUBVERSION $(KERNEL_MIDDLE_VER)" >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_RELEASE $(KERNEL_MINOR_VER)" >> $(VERFILE)
	$(Q)$(ECHO) >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_RELEASE_NAME \"$(KERNEL_NAME)\"" >> $(VERFILE)
	$(Q)$(ECHO) >> $(VERFILE)
	$(Q)$(ECHO) "#endif /* __VERSION_H__ */" >> $(VERFILE)

config: host
	$(Q)$(MKDIR) -p $(BUILD_ROOT)/include/config
	$(Q)$(MAKE) -C kbuild conf BUILD_ROOT=$(BUILD_ROOT)
	$(Q)$(BUILD_ROOT)/kbuild/conf $(BUILD_ROOT)/eza/arch/$(arch)/Kconfig
	$(Q)$(ECHO) "ARCH=$(arch)" >> .config

menuconfig: host
	$(Q)$(MKDIR) -p $(BUILD_ROOT)/include/config
	$(Q)$(MAKE) mconf -C kbuild BUILD_ROOT=$(BUILD_ROOT)
	$(Q)$(BUILD_ROOT)/kbuild/mconf $(BUILD_ROOT)/eza/arch/$(arch)/Kconfig
	$(Q)$(BUILD_ROOT)/kbuild/conf -s $(BUILD_ROOT)/eza/arch/$(arch)/Kconfig
	$(Q)$(ECHO) "ARCH=$(arch)" >> .config

help:
	$(Q)$(ECHO) "USAGE: make [action] [OPTIONS] [VARIABLES]"
	$(Q)$(ECHO) "   OPTIONS:"
	$(Q)$(ECHO) "      NOCOLOR=[y/n]       Enable/disable color (default: enabled)"
	$(Q)$(ECHO) "      VERBOSE=[y/n]       Enable/disable verbose mode (default: disabled)"
	$(Q)$(ECHO) "      target=<dir>        Specify building directory"
	$(Q)$(ECHO) "      TOOLCHAIN=<prefix>  Specify toolchain prefix"
	$(Q)$(ECHO) "      NOBUILDIMG=[y/n]    Toggle image building"
	$(Q)$(ECHO) "    Customizible variables:"
	$(Q)$(ECHO) "      CFLAGS, LDFLAGS, INCLUDE, HOSTCC, HOSTCFLAGS, HOSTLDFLAGS"
	$(Q)$(ECHO) "    Available actions:"
	$(Q)$(ECHO) "      make [config|menuconfig] arch=<your_arch> - configure the kernel"
	$(Q)$(ECHO) "      make all - build all"
	$(Q)$(ECHO) "      make host - build host utilites"
	$(Q)$(ECHO) "      make vmuielf - build kernel image"
	$(Q)$(ECHO) "      make clean - clean directories from object files"
	$(Q)$(ECHO) "      make cleanconf - remove config"
	$(Q)$(ECHO) "      make distclean - combines two actions above"

help_config:
	$(Q)$(ECHO) "Before building kernel you should configure it"
	$(Q)$(ECHO) "Run make [config or menuconfig] arch=<your_arch>"
	$(Q)$(ECHO) "Supported architectures:"
	$(call show_archs)
	$(Q)$(ECHO)
	$(Q)exit 2

