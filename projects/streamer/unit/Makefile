#author:chenzhengqiang
#generate date:20151118-08:54:46


INCLUDE_DIR:=../include/
SOURCE_DIR:=../src/

SUFFIX:=cpp
vpath %.h $(INCLUDE_DIR)
vpath %.$(SUFFIX) $(SOURCE_DIR)

TARGET:=test_rose_http
CC:=g++
CFLAGS:=-O0 -g -W -Wall -Werror -Wshadow
LDCONFIG:=-I$(INCLUDE_DIR) -lev -lpthread
OBJS:=system_info streamer state_server rosehttp parson netutility logging test_rose_http
OBJS:=$(foreach obj,$(OBJS),$(obj).o)

INSTALL_DIR:=/usr/local/bin
TAR_NAME=$(TARGET)-$(shell date '+%Y%m%d')

.PHONEY=clean
.PHONEY=install
.PHONEY=test
.PHONEY=tar

all:$(TARGET)
$(TARGET):$(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDCONFIG)
$(OBJS):%.o:%.$(SUFFIX)
	$(CC) -o $@ -c $< $(CFLAGS) $(LDCONFIG)

clean:
	-rm -f *.o *.a *.so *.log *core* $(TARGET) *.tar.gz *.cppe

install:
	-rm -f $(INSTALL_DIR)/$(TARGET)
	-mv $(TARGET) $(INSTALL_DIR)

test:
	./$(TARGET)
tar:
	tar -cvzf $(TAR_NAME).tar.gz .
