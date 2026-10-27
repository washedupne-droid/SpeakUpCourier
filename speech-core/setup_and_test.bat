@echo off
cd /d "%~dp0"
where python >nul 2>nul || (echo Python not found. Install 64-bit Python from https://www.python.org/downloads/ and tick "Add python.exe to PATH". & pause & exit /b 1)
echo Installing Python packages...
python -m pip install -q -r requirements.txt || (pause & exit /b 1)
if not exist vosk-model-small-en-us-0.15 (
  echo Downloading the speech model, about 40 MB...
  powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip -OutFile model.zip; Expand-Archive model.zip -DestinationPath .; Remove-Item model.zip"
)
python quick_test.py
pause
