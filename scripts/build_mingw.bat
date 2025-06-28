@echo off
gcc -I..\include -O3 -s -static -shared ..\src\*.c ..\timidity\*.c -o timidity.clap
