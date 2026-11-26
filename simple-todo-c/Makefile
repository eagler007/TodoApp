CC = x86_64-w64-mingw32-gcc
WINDRES = x86_64-w64-mingw32-windres
CFLAGS = -O2 -mwindows -I. -Ilib
LDFLAGS = -lcomctl32 -lshell32 -lcomdlg32 -luuid -lwininet -lgdi32 -lgdiplus -ladvapi32

OBJS = main.o app.o app_ui.o theme.o config.o weather.o startup.o ui.o calendar.o pie.o trend.o dlg.o menu.o todo.o account.o storage.o util.o lib/cJSON.o resource.o

all: TodoApp.exe

lib/cJSON.o: lib/cJSON.c lib/cJSON.h
	$(CC) $(CFLAGS) -c lib/cJSON.c -o lib/cJSON.o

resource.o: resource.rc
	$(WINDRES) -c 65001 -i resource.rc -o resource.o

util.o: util.c util.h
	$(CC) $(CFLAGS) -c util.c -o util.o

theme.o: theme.c theme.h
	$(CC) $(CFLAGS) -c theme.c -o theme.o

ui.o: ui.c ui.h theme.h
	$(CC) $(CFLAGS) -c ui.c -o ui.o

storage.o: storage.c storage.h util.h lib/cJSON.h
	$(CC) $(CFLAGS) -c storage.c -o storage.o

todo.o: todo.c todo.h util.h
	$(CC) $(CFLAGS) -c todo.c -o todo.o

account.o: account.c account.h util.h
	$(CC) $(CFLAGS) -c account.c -o account.o

app_ui.o: app_ui.c app_ui.h app.h ui.h theme.h util.h calendar.h menu.h
	$(CC) $(CFLAGS) -c app_ui.c -o app_ui.o

pie.o: pie.c pie.h ui.h theme.h util.h
	$(CC) $(CFLAGS) -c pie.c -o pie.o

trend.o: trend.c trend.h ui.h theme.h util.h
	$(CC) $(CFLAGS) -c trend.c -o trend.o

calendar.o: calendar.c calendar.h ui.h theme.h
	$(CC) $(CFLAGS) -c calendar.c -o calendar.o

dlg.o: dlg.c dlg.h ui.h theme.h calendar.h util.h
	$(CC) $(CFLAGS) -c dlg.c -o dlg.o

menu.o: menu.c menu.h ui.h theme.h
	$(CC) $(CFLAGS) -c menu.c -o menu.o

startup.o: startup.c startup.h config.h
	$(CC) $(CFLAGS) -c startup.c -o startup.o

app.o: app.c app.h app_ui.h dlg.h ui.h theme.h todo.h account.h storage.h util.h startup.h lib/cJSON.h
	$(CC) $(CFLAGS) -c app.c -o app.o

main.o: main.c app.h app_ui.h
	$(CC) $(CFLAGS) -c main.c -o main.o

TodoApp.exe: $(OBJS)
	$(CC) $(CFLAGS) -s -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o lib/*.o TodoApp.exe

.PHONY: all clean
