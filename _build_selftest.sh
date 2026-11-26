#!/bin/sh
# 自检程序：纯控制台，不弹窗。跑一遍底层逻辑，改完代码立刻验证
set -e
export PATH="/d/WorkBuddy/tools/w64devkit/bin:$PATH"
cd /d/WorkBuddy/Todo程序/simple-todo-c

CFLAGS="-O2 -I. -Ilib -Wall"

gcc $CFLAGS -c selftest.c -o selftest.o
gcc $CFLAGS -c account.c  -o account_st.o
gcc $CFLAGS -c todo.c     -o todo_st.o
gcc $CFLAGS -c storage.c  -o storage_st.o
gcc $CFLAGS -c util.c     -o util_st.o
gcc $CFLAGS -c lib/cJSON.c -o cjson_st.o

gcc -s -o TodoSelfTest.exe selftest.o account_st.o todo_st.o storage_st.o util_st.o cjson_st.o \
    -lcomdlg32 -lgdi32

rm -f selftest.o account_st.o todo_st.o storage_st.o util_st.o cjson_st.o || true
ls -l TodoSelfTest.exe
