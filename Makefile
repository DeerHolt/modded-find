CFLAGS=-Wall -Wextra -O2 -Werror
TARGETS=PluginLib.so PluginLauncher
LIBTARGETS=PluginLib.c plug_helpers.c plug_helpers.h plugin_api.h

.PHONY: all clean PluginLib

all: clean $(TARGETS) PluginLib

clean:
	rm -rf *.o $(TARGETS)

PluginLauncher: PluginLauncher.c plug_helpers.c plug_helpers.h plugin_api.h
	gcc $(CFLAGS) PluginLauncher.c plug_helpers.c -o $@ -ldl

PluginLib: $(LIBTARGETS)
	gcc $(CFLAGS) -fPIC -shared PluginLib.c plug_helpers.c -o PluginLib.so -ldl -lm

