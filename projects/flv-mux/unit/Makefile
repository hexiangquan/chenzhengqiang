#@Author:chenzhengqiang
#@Date: 2015/8/25
#@Version: v1.0 v1.1


INCLUDE_DIR:=../include/
SOURCE_DIR:=../src/

SUFFIX:=cpp
vpath %.h $(INCLUDE_DIR)
vpath %.$(SUFFIX) $(SOURCE_DIR)

TARGET:=flv_muxer
CC:=g++
OBJS:=Main AudioTag FileIo Flv Mux Mybs Mycrc_32 ScriptTag VideoTag

CFLAGS:=-g -W -Wall -I$(INCLUDE_DIR)

INSTALL_DIR:=/usr/local/bin
CONFIG_FILE:=./config/streamer.conf
NOTIFY_FILE:=./config/notify_server.conf
CONFIG_INSTALL_PATH:=/etc/streamer
SERVICE:=./scripts/streamer
TAR_NAME=$(TARGET)-$(shell date '+%Y%m%d')

OBJS:=$(foreach obj,$(OBJS),$(obj).o)

.PHONEY=clean
.PHONEY=install
.PHONEY=test
.PHONEY=tar

all:$(TARGET)
$(TARGET):$(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)
$(OBJS):%.o:%.$(SUFFIX)
	$(CC) -o $@ -c $< -I$(INCLUDE_DIR) $(CFLAGS)

clean:
	-rm -f *.o *.a *.so *.log *core* $(TARGET) *.tar.gz *.cppe
install:
	-mv $(TARGET) $(INSTALL_DIR)
	-cp -f $(SERVICE) /etc/init.d/$(TARGET)
	-rm -rf $(CONFIG_INSTALL_PATH)
	-mkdir $(CONFIG_INSTALL_PATH)
	-cp -f $(CONFIG_FILE) $(CONFIG_INSTALL_PATH)
	-cp -f $(NOTIFY_FILE) $(CONFIG_INSTALL_PATH)
test:
	./$(TARGET) -f ./config/streamer.conf
tar:
	tar -cvzf $(TAR_NAME).tar.gz .  
