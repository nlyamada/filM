# filM - ビルド設定 (MinGW-w64 / g++)

CXX      := g++
WINDRES  := windres
TARGET   := bin/filM.exe

SRC_DIR  := src
RES_DIR  := res
OBJ_DIR  := obj

SOURCES  := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS  := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SOURCES))
RES_OBJ  := $(OBJ_DIR)/app_res.o

CXXFLAGS := -std=c++17 -municode -DUNICODE -D_UNICODE \
            -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
            -Wall -Wextra -O2 -I$(SRC_DIR) -isystem third_party/webview2 -MMD -MP

# MinGW のランタイム DLL (libstdc++-6.dll 等) を exe に取り込み、PATH に MinGW が無い環境でも
# 起動できるようにする (配布・別 PC への持ち運びのため)。
LDFLAGS  := -municode -mwindows -static -static-libgcc -static-libstdc++
LIBS     := -lcomctl32 -ldwmapi -luxtheme -limm32 -lmsimg32 -lshell32 -lole32 -luuid -lcomdlg32 -lwindowscodecs -lgdi32 -luser32

.DEFAULT_GOAL := all
.PHONY: all assets clean run test

# GUI 非依存ロジックのテスト (main.cpp 以外を結合)
TEST_BIN := bin/test_core.exe
TEST_OBJS := $(filter-out $(OBJ_DIR)/main.o,$(OBJECTS))

test: $(TEST_OBJS) tests/test_core.cpp | bin
	$(CXX) $(CXXFLAGS) -c tests/test_core.cpp -o $(OBJ_DIR)/test_core.o
	$(CXX) $(OBJ_DIR)/test_core.o $(TEST_OBJS) -o $(TEST_BIN) $(LIBS)
	./$(TEST_BIN)

all: $(TARGET) assets

# Markdown ビューアーの実行時ファイル (WebView2Loader.dll と viewer/) を exe の隣へ置く。
assets: | bin
	cp -f third_party/webview2/WebView2Loader.dll bin/
	mkdir -p bin/viewer
	cp -rf viewer/. bin/viewer/

$(TARGET): $(OBJECTS) $(RES_OBJ) | bin
	$(CXX) $(OBJECTS) $(RES_OBJ) -o $@ $(LDFLAGS) $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RES_OBJ): $(RES_DIR)/app.rc $(RES_DIR)/app.manifest $(RES_DIR)/filM.ico | $(OBJ_DIR)
	$(WINDRES) -I$(RES_DIR) $< -O coff -o $@

-include $(OBJECTS:.o=.d)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

bin:
	mkdir -p bin

run: all
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) bin


