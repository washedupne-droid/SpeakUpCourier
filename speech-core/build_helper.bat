@echo off
cd /d "%~dp0"
python -m pip install -q pyinstaller || (pause & exit /b 1)
python -m PyInstaller --noconfirm --clean --noupx --onedir --collect-all vosk --name vdm_helper vdm_helper.py || (pause & exit /b 1)
echo.
echo Done. The helper is in dist\vdm_helper. Copy that folder's contents to
echo Data\NVSE\Plugins\VoiceDialogue\helper in your game folder.
pause
