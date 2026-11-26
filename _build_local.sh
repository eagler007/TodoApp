#!/bin/sh
# 本地编译：远端 192.168.2.13 不可达时的备用链路（w64devkit 便携 mingw-w64）
set -e
export PATH="/d/WorkBuddy/tools/w64devkit/bin:$PATH"
cd /d/WorkBuddy/Todo程序/simple-todo-c

CFLAGS="-O2 -mwindows -I. -Ilib"
LDFLAGS="-lcomctl32 -lshell32 -lcomdlg32 -luuid -lwininet -lgdi32 -lgdiplus -ladvapi32"

make clean >/dev/null 2>&1 || rm -f *.o lib/*.o 2>/dev/null || true

for f in main app app_ui theme config weather startup ui calendar pie trend dlg menu todo account storage util; do
    echo "  CC $f.c"
    gcc $CFLAGS -c $f.c -o $f.o
done
echo "  CC lib/cJSON.c"
gcc $CFLAGS -c lib/cJSON.c -o lib/cJSON.o
echo "  RC resource.rc"
windres --codepage=65001 -i resource.rc -o resource.o

echo "  LINK TodoApp.exe"
gcc $CFLAGS -s -o TodoApp.exe main.o app.o app_ui.o theme.o config.o weather.o startup.o ui.o calendar.o pie.o trend.o dlg.o menu.o \
    todo.o account.o storage.o util.o lib/cJSON.o resource.o $LDFLAGS

ls -l TodoApp.exe
