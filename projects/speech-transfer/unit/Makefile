#@Author:chenzhengqiang
#@Company:swwy
#@Date: 2015/6/23
#@Version: v1.0
#@Desc

SUFFIX:=cpp
vpath %.h ../include/
vpath %.$(SUFFIX) ../src/

TARGET:=camera
CC:=g++
#OBJS:=test_mix netutility logging rtp aes opus signalling speech_mix
<<<<<<< HEAD
OBJS:=test_camera logging netutility rtp aes opus signalling
CFLAGS:=-g -lpthread -lcrypto -lopus -W -Wall
=======
OBJS:=test_camera ortp_send ortp_recv logging netutility rtp aes opus signalling
CFLAGS:=-g -lpthread -lcrypto -lopus -lortp -W -Wall -lortp -lpulse -lpulse-simple
>>>>>>> e6d426d07836910f817ca8afae19c7a271327efe
INCLUDE_DIR:=./../include/
INSTALL_DIR:=/usr/local/bin
CONFIG_FILE:=./config/server.conf
#NOTIFY_FILE:=./config/notify_server.conf
CONFIG_INSTALL_PATH:=/etc/transfer
SERVICE:=./scripts/transfer
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
	-rm -f *.o *.a *.so *.log *core* *.tar.gz
install:
	-mv $(TARGET) $(INSTALL_DIR)
	-cp -f $(SERVICE) /etc/init.d/$(TARGET)
	-rm -rf $(CONFIG_INSTALL_PATH)
	-mkdir $(CONFIG_INSTALL_PATH)
	-cp -f $(CONFIG_FILE) $(CONFIG_INSTALL_PATH)
test:
	./$(TARGET) -f $(CONFIG_FILE)
tar:
	tar -cvzf $(TAR_NAME).tar.gz .  
