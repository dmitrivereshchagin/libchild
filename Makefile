DLL = child.dll

SRCS = $(wildcard *.c)
DEPS = $(patsubst %.c, %.d, $(SRCS))
OBJS = $(patsubst %.c, %.o, $(SRCS))

CPPFLAGS += -D_EXPORTING
LDFLAGS  += -shared
LDLIBS   += -lrpcrt4

.PHONY: all clean

all: $(DLL)

$(DLL): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) -MM $< -MF $*.d
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

-include $(DEPS)

clean:
	$(RM) $(DLL) $(DEPS) $(OBJS)
