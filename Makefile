#
# Copyright (C) 2020 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=soft_pwm
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0

PKG_MAINTAINER:=Elad Yifee <eladwf@gmail.com>

include $(INCLUDE_DIR)/package.mk

define KernelPackage/soft_pwm
  SECTION:=kernel
  SUBMENU:=Other modules
  TITLE:=Soft pwm
  FILES:=$(PKG_BUILD_DIR)/soft_pwm.ko
  AUTOLOAD:=$(call AutoLoad,30,soft_pwm,1)
endef

define KernelPackage/soft_pwm/description
  Enable soft-pwm.

endef

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(KERNEL_MAKE_FLAGS) \
		M="$(PKG_BUILD_DIR)" \
		EXTRA_CFLAGS="$(BUILDFLAGS)" \
		modules
endef

$(eval $(call KernelPackage,soft_pwm))
