CC ?= gcc
PKG_CONFIG ?= pkg-config

CFLAGS += -Wall -Wextra -O2 $(shell $(PKG_CONFIG) --cflags gtk+-2.0 cairo)
LDLIBS += $(shell $(PKG_CONFIG) --libs gtk+-2.0 cairo) -lm

OBJS = main.o xiangqi_engine.o pikafish_uci.o touch_input.o
TEST_OBJS = smoke_test.o xiangqi_engine.o

.PHONY: all clean

all: exact-chinesechess

exact-chinesechess: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

smoke-test: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDLIBS)

touch-test: touch_input_test.c touch_input.c touch_input.h
	$(CC) -std=c99 -Wall -Wextra -O2 -o $@ touch_input_test.c touch_input.c -lm

ui-test: ui_integration_test.c main.c xiangqi_engine.o pikafish_uci.o touch_input.o
	$(CC) $(CFLAGS) -o $@ ui_integration_test.c xiangqi_engine.o pikafish_uci.o touch_input.o $(LDLIBS)

clean:
	rm -f $(OBJS) $(TEST_OBJS) exact-chinesechess smoke-test touch-test ui-test
