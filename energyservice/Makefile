include $(TOPDIR)/rules.mk

PKG_NAME:=energyservice
PKG_RELEASE:=14
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_RELEASE)

include $(INCLUDE_DIR)/package.mk

define Package/energyservice
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=AMX EnergyService configuration daemon
  DEPENDS:=+amxrt +tr181-device +libamxc +libamxd +libamxo +libamxp +libsahtrace
endef

define Package/energyservice/description
 AMX datamodel and runtime configuration daemon for the GW Energy Application.
endef

define Package/energyservice/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || /etc/init.d/energyservice enable
exit 0
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
	# The source directory may contain host-side/prebuilt artifacts from
	# development. Always compile the shared object from the current C source.
	rm -f $(PKG_BUILD_DIR)/energyservice.o $(PKG_BUILD_DIR)/energyservice.so
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		STAGING_DIR="$(STAGING_DIR)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/lib -L$(STAGING_DIR)/usr/lib"
endef

define Package/energyservice/install
	$(INSTALL_DIR) $(1)/etc/amx/energyservice
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/energyservice
	$(INSTALL_DIR) $(1)/usr/lib/amx/energyservice
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/energyservice.init $(1)/etc/init.d/energyservice
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/energyservice.odl $(1)/etc/amx/energyservice/energyservice.odl
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/energyservice_defaults.odl $(1)/etc/amx/energyservice/energyservice_defaults.odl
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/energyservice_definition.odl $(1)/etc/amx/energyservice/energyservice_definition.odl
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/gw_energy_app.config $(1)/etc/energyservice/gw_energy_app.config
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/gw_energy_app.conf $(1)/etc/energyservice/gw_energy_app.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/energyservice.so $(1)/usr/lib/amx/energyservice/energyservice.so
	ln -s /usr/bin/amxrt $(1)/usr/bin/energyservice
endef

$(eval $(call BuildPackage,energyservice))
