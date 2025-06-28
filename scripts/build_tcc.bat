@echo off
tcc -I..\include -shared ..\src\*.c ..\timidity\*.c -o timidity.clap
