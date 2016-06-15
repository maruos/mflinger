PACKAGE := mflinger

MAJOR_VERSION := 0
MINOR_VERSION := 1

ARCHIVE := $(PACKAGE)-$(MAJOR_VERSION).$(MINOR_VERSION)

#
# Compiler
#
CC = gcc
CFLAGS = -Wall
LIBS = -lX11 -lXfixes -lXext -lXdamage -lXi -lXrandr -lpthread
INCLUDES = -Iinclude 

#
# Build
#
BUILD_OUT := out

#
# Modules
#
TARGET_LIB_MODULE := libmflinger
TARGET_LIB := lib/$(TARGET_LIB_MODULE).a
LIB_SRCS := $(wildcard lib/*.c)
LIB_OBJS := $(patsubst %.c,%.o,$(LIB_SRCS)) 
TARGET_LIB_DEPS := $(LIB_OBJS) 
LDFLAGS += -Llib
LIBS += -lmflinger

TARGET_MODULE := mclient
TARGET := $(BUILD_OUT)/$(TARGET_MODULE)
SRCS := $(wildcard mclient/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS)) 
TARGET_DEPS := $(OBJS) $(TARGET_LIB) 

#
# Rules
#
.PHONY: all install uninstall dist deb clean

all: $(TARGET)

$(TARGET): $(BUILD_OUT) $(TARGET_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

$(TARGET_LIB): $(TARGET_LIB_DEPS) 
	ar rcs $@ $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_OUT):
	@mkdir -p $@ 


prefix=/usr/local
install: $(TARGET)
	mkdir -p $(DESTDIR)$(prefix)/bin
	cp $< $(DESTDIR)$(prefix)/bin/$(TARGET_MODULE)

uninstall:
	rm -f $(DESTDIR)$(prefix)/bin/$(TARGET_MODULE)

dist: $(BUILD_OUT)
	mkdir -p /tmp/dist/$(ARCHIVE)
	cp -r * /tmp/dist/$(ARCHIVE)
	rm -r /tmp/dist/$(ARCHIVE)/$(BUILD_OUT)
	mv /tmp/dist/$(ARCHIVE) -t $(BUILD_OUT)
	tar cJf $(BUILD_OUT)/$(ARCHIVE).tar.xz -C $(BUILD_OUT) $(ARCHIVE)

DEBPKG_NAME := maru-mflinger-client
DEBPKG_SRC_ARCHIVE := $(DEBPKG_NAME)_$(MAJOR_VERSION).$(MINOR_VERSION).orig.tar.xz
DEBPKG_BUILD_OPTS = -us -uc
deb:
	cd $(BUILD_OUT) && \
	cp -r ../build/debian $(ARCHIVE) && \
	cp $(ARCHIVE).tar.xz $(DEBPKG_SRC_ARCHIVE) && \
	cd $(ARCHIVE) && debuild $(DEBPKG_BUILD_OPTS)

clean:
	-@rm $(OBJS) $(LIB_OBJS)
	-@rm $(TARGET_LIB)
	-@rm -r $(BUILD_OUT)

.DELETE_ON_ERROR:
