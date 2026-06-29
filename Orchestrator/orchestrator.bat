@echo off
rem Open the preset editor (GUI) without leaving a console window behind.
rem pythonw runs the Tk app windowless; start detaches so this cmd window closes.
start "" pythonw "%~dp0src\json_editor.py" %*
