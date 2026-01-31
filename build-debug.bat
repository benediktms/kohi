@echo off

if not exist misc\ktime.exe (
    make setup
)

misc\ktime.exe make build-debug
