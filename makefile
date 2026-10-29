install: #https://docs.godotengine.org/en/latest/engine_details/development/configuring_an_ide/visual_studio.html
	py -m pip install scons
	py misc\scripts\install_accesskit.py
	py misc\scripts\install_d3d12_sdk_windows.py
	py -m SCons platform=windows vsproj=yes dev_build=yes