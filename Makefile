#
# Copyright 2015-2016 Preetam J. D'Souza
# Copyright 2016 The Maru OS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
PACKAGE := mflinger

VERSION := $(shell git describe)

ARCHIVE := $(PACKAGE)-$(VERSION)

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
SRCS := $(wildcard src/mclient/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS)) 
TARGET_DEPS := $(OBJS) $(TARGET_LIB) 

TEST_MODULE := suite
TEST_TARGET := tests/$(TEST_MODULE)
TEST_SRCS := $(wildcard tests/*.c)
TEST_OBJS := $(patsubst %.c,%.o,$(TEST_SRCS))
TEST_TARGET_DEPS := $(TEST_OBJS) \
	src/mclient/util.o

#
# Rules
#
.PHONY: all debug install uninstall dist clean

all: $(TARGET)

debug: CFLAGS += -g -O0 -DDEBUG
debug: all

$(TARGET): $(BUILD_OUT) $(TARGET_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

$(TARGET_LIB): $(TARGET_LIB_DEPS) 
	ar rcs $@ $<

tests: $(TEST_TARGET)
$(TEST_TARGET): $(TEST_TARGET_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

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

clean:
	-@rm $(OBJS) $(LIB_OBJS) $(TEST_OBJS)
	-@rm $(TARGET) $(TARGET_LIB) $(TEST_TARGET)
	-@rm -r $(BUILD_OUT)

.DELETE_ON_ERROR:
