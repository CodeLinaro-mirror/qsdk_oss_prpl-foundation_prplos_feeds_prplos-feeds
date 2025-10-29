include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/version.mk

PKG_NAME:=wireless-freedom
PKG_RELEASE:=1
PKG_FLAGS:=nonshared
PKG_LICENSE:=BSD-2-Clause-Patent

include $(INCLUDE_DIR)/package.mk

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Compile
endef

define Package/wireless-freedom
  SUBMENU:=ath12k Board-Specific Overrides
  SECTION:=freedom
  CATEGORY:=uci
  TITLE:=Freedom wireless UCI extension
endef

define Package/wireless-freedom/install
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/wireless-freedom $(1)/etc/uci-defaults/50-wireless-freedom
endef

$(eval $(call BuildPackage,wireless-freedom))
