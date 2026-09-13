@echo off
cd /d "%~dp0"
start "ECG Viewer" "build\ecg_viewer.exe" COM7
