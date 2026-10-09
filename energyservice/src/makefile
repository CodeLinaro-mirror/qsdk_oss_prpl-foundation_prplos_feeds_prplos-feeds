CC ?= cc
STAGING_DIR ?=
TARGET := energyservice.so
OBJ := energyservice.o

CPPFLAGS += -I$(STAGING_DIR)/usr/include -I$(STAGING_DIR)/include
CFLAGS += -std=c11 -Wall -Wextra -Werror -fPIC $(CPPFLAGS)
LDLIBS += -lamxc -lamxd -lamxo -lamxp -lsahtrace

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -shared $(LDFLAGS) -o $@ $^ $(LDLIBS)

energyservice.o: energyservice.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJ)
