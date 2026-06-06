CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LDFLAGS = $(shell sdl2-config --libs)
TTF_LDFLAGS = -lSDL2_ttf

TARGET = sdl_tricorder
SRC = sdl_tricorder.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(SRC) -o $(TARGET) $(SDL_LDFLAGS) $(TTF_LDFLAGS)

clean:
	rm -f $(TARGET)
