ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src tests

sysconfscoutdir = $(sysconfdir)/scout
dist_sysconfscout_DATA = conf/scout.conf

clawservicesdir = $(sysconfdir)/claw/services.d
dist_clawservices_DATA = conf/scout.service.yml

EXTRA_DIST = \
	README.md \
	autogen.sh \
	conf/scout.conf \
	conf/scout.service.yml \
	pkg/meta/manifest.json.in \
	pkg/meta/scripts/postinst \
	pkg/meta/scripts/postrm \
	pkg/meta/scripts/preinst \
	pkg/meta/scripts/prerm \
	tools/build-musl.sh \
	tools/configure-blueyos.sh \
	tools/resolve-musl-cc.sh \
	tools/resolve-musl-prefix.sh

PACKAGE_STAGE_DIR = $(abs_top_builddir)/package-stage
DIST_OUTPUT_DIR = $(abs_top_builddir)/dist
DIMSIM_DPKBUILD ?= dpkbuild

PACKAGE_BUILD_NUMBER ?= @PACKAGE_BUILD_NUMBER@
DIMSIM_PACKAGE_VERSION = $(PACKAGE_VERSION)-$(PACKAGE_BUILD_NUMBER)

install-sysroot:
	@if test -z "$(SYSROOT)" && test -z "$(DESTDIR)"; then \
		echo "Set SYSROOT=/path/to/rootfs (or DESTDIR=/path/to/rootfs)"; \
		exit 1; \
	fi
	@dest="$(if $(DESTDIR),$(DESTDIR),$(SYSROOT))"; \
	if test "$$dest" = "/" || test "$$dest" = "."; then \
		echo "Refusing to install into unsafe destination '$$dest'"; \
		exit 1; \
	fi; \
	$(MAKE) DESTDIR="$$dest" install

package: all
	@command -v "$(DIMSIM_DPKBUILD)" >/dev/null 2>&1 || { \
		echo "dpkbuild not found on PATH"; \
		exit 1; \
	}
	@rm -rf "$(PACKAGE_STAGE_DIR)" "$(DIST_OUTPUT_DIR)"
	@mkdir -p "$(PACKAGE_STAGE_DIR)/meta/scripts" "$(DIST_OUTPUT_DIR)"
	@cp "$(abs_top_srcdir)/pkg/meta/scripts/preinst" "$(PACKAGE_STAGE_DIR)/meta/scripts/preinst"
	@cp "$(abs_top_srcdir)/pkg/meta/scripts/postinst" "$(PACKAGE_STAGE_DIR)/meta/scripts/postinst"
	@cp "$(abs_top_srcdir)/pkg/meta/scripts/prerm" "$(PACKAGE_STAGE_DIR)/meta/scripts/prerm"
	@cp "$(abs_top_srcdir)/pkg/meta/scripts/postrm" "$(PACKAGE_STAGE_DIR)/meta/scripts/postrm"
	@chmod 0755 "$(PACKAGE_STAGE_DIR)/meta/scripts/preinst" \
		"$(PACKAGE_STAGE_DIR)/meta/scripts/postinst" \
		"$(PACKAGE_STAGE_DIR)/meta/scripts/prerm" \
		"$(PACKAGE_STAGE_DIR)/meta/scripts/postrm"
	@sed 's/__PACKAGE_VERSION__/$(DIMSIM_PACKAGE_VERSION)/g' \
		"$(abs_top_srcdir)/pkg/meta/manifest.json.in" > "$(PACKAGE_STAGE_DIR)/meta/manifest.json"
	@$(MAKE) DESTDIR="$(PACKAGE_STAGE_DIR)/payload" install
	@( cd "$(DIST_OUTPUT_DIR)" && "$(DIMSIM_DPKBUILD)" build "$(PACKAGE_STAGE_DIR)" )
	@echo "Built scout-$(DIMSIM_PACKAGE_VERSION)-i386.dpk in $(DIST_OUTPUT_DIR)"

clean-local:
	@rm -rf "$(PACKAGE_STAGE_DIR)" "$(DIST_OUTPUT_DIR)"
