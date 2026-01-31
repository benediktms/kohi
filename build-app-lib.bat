@echo off
REM Builds only the testbed library

if not exist misc\ktime.exe (
    make setup
)

misc\ktime.exe make testbed-debug
