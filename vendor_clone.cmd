powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\tools\vendor-update.ps1"
git clone --depth 1 --branch 3.5-stable https://github.com/godotengine/godot.git vendor/godot-3.5
git clone --depth 1 --branch 3.5.3-stable https://github.com/godotengine/godot.git vendor/godot-3.5.3
git clone --depth 1 --branch 3.6.2-stable https://github.com/godotengine/godot.git vendor/godot-3.6.2
git clone --depth 1 --branch 4.0.4-stable https://github.com/godotengine/godot.git vendor/godot-4.0.4
git clone --depth 1 --branch 4.1.4-stable https://github.com/godotengine/godot.git vendor/godot-4.1.4
git clone --depth 1 --branch 4.2.2-stable https://github.com/godotengine/godot.git vendor/godot-4.2.2
git clone --depth 1 --branch 4.3-stable https://github.com/godotengine/godot.git vendor/godot-4.3
git clone --depth 1 --branch 4.4.1-stable https://github.com/godotengine/godot.git vendor/godot-4.4.1
git clone --depth 1 --branch 4.5.1-stable https://github.com/godotengine/godot.git vendor/godot-4.5.1
git clone --depth 1 --branch 4.2-stable https://github.com/godotengine/godot.git vendor/godot-4.2
git clone --depth 1 --branch 4.1-stable https://github.com/godotengine/godot.git vendor/godot-4.1
git clone --depth 1 --branch 4.6.1-stable https://github.com/godotengine/godot.git vendor/godot-4.6.1
git clone --depth 1 --branch 3.x https://github.com/godotengine/godot.git vendor/godot-3.x

# 4.5.1 rc2
mkdir vendor/godot-4.5.1-rc2
cd vendor/godot-4.5.1-rc2
git init
git remote add origin https://github.com/godotengine/godot.git
git fetch --depth 1 origin f782f6ad05cae079d1e708d1c5c11f43910f6b8e
git checkout FETCH_HEAD