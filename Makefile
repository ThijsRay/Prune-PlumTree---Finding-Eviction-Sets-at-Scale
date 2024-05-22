CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O0 -g
LDFLAGS = -lrt -lm

TARGET = main
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(TARGET) pruneplumtree.o

pruneplumtree.o: plumtree.o plumtree_utils.o
	ld -r $^ -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET) pruneplumtree.o

