#!/bin/sh
# 仅用于离线截图：带 -DCAP_HOOK 的调试构建，产出 TodoApp_cap.exe（不参与交付）
set -e
export PATH="/d/WorkBuddy/tools/w64devkit/bin:$PATH"
cd /d/WorkBuddy/Todo程序/simple-todo-c

CFLAGS="-O2 -mwindows -I. -Ilib -DCAP_HOOK"
LDFLAGS="-lcomctl32 -lshell32 -lcomdlg32 -luuid -lwininet -lgdi32 -lgdiplus -ladvapi32"

gcc $CFLAGS -c app.c -o app_cap.o || exit 1
gcc $CFLAGS -c pie.c -o pie.o || exit 1
gcc $CFLAGS -c trend.c -o trend.o || exit 1
gcc $CFLAGS -s -o TodoApp_cap.exe main.o app_cap.o app_ui.o theme.o config.o weather.o startup.o ui.o calendar.o pie.o trend.o dlg.o menu.o \
    todo.o account.o storage.o util.o lib/cJSON.o resource.o $LDFLAGS
ls -l TodoApp_cap.exe
