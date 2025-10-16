include $(TOPDIR)/rules.mk

PKG_NAME:=netifd-scripts
PKG_VERSION:=1.0

include $(INCLUDE_DIR)/package.mk

define Package/netifd-scripts
  SECTION:=WLAN-INTERNAL
  CATEGORY:=QTI software
  TITLE:=Netifd scripts for WiFi
endef

define Package/netifd-scripts/description
  netifd-scripts holds the scripts required to bring up WiFi
endef

define Build/Compile
endef

define Package/netifd-scripts/install
	mkdir -p $(1)/lib/netifd/
	$(INSTALL_DIR) $(1)/lib/netifd/
	$(INSTALL_BIN) ./files/utils.sh $(1)/lib/netifd/
	$(INSTALL_BIN) ./files/netifd-proto.sh $(1)/lib/netifd/
endef

$(eval $(call BuildPackage,netifd-scripts))
