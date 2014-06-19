NAME = bsp2bmp

CC = gcc

SRCS = $(wildcard *.c)
OBJS = $(subst .c,.o,$(SRCS))

ARCS = CHANGELOG COPYING INSTALL README Makefile $(SRCS)
ARCDIR := $(shell basename $$PWD)

#OFLAGS = -Wall -fpack-struct
OFLAGS = -Wall -fpack-struct -m32
LFLAGS = -s -lm -m32

.PHONY: all msg
.SUFFIXES: .o .c

all : msg $(NAME)
	@echo Done!!!

msg : 
	@echo Making...

$(NAME) : $(OBJS)
	$(CC) -o $(NAME) $(OBJS) $(LFLAGS)

.c.o :
	$(CC) -c -o $(NAME).o $(NAME).c $(OFLAGS)

clean :
	rm -f $(NAME)
	rm -f *.o

archive : $(ARCS)
	cd ..; tar -czvf $(ARCDIR)/$(ARCDIR).tar.gz `for A in $(ARCS); do echo $(ARCDIR)/$$A; done`; cd $(ARCNAME)
	chmod 644 $(ARCDIR).tar.gz
