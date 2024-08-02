PKG_CONFIG ?= pkg-config
CC ?= cc
LD = $(CC)

OUT=undmg
SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)
LIB=$(shell $(PKG_CONFIG) --libs zlib) $(shell $(PKG_CONFIG) --libs bzip2) -llzfse -llzma -lc
PREFIX=/usr/local
TARGET=$(PREFIX)/bin/$(OUT)
LDFLAGS=
CFLAGS=$(shell $(PKG_CONFIG) --cflags zlib) $(shell $(PKG_CONFIG) --cflags bzip2)

.PHONY: all
all: $(OUT)

$(OUT): $(OBJ)
	$(LD) $(LDFLAGS) $(LIB) $^ -o $@

.PHONY: install
install: $(TARGET)

$(TARGET): $(OUT)
	mkdir -p $(PREFIX)/bin/
	install $(OUT) $(PREFIX)/bin/

%.o: %.c
	$(CC) -c $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	rm -f $(OBJ) $(OUT)

.PHONY: test
test: $(OUT)
	$(SHELL) ./test.sh
