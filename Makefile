CC = clang
CXX = clang++

CFLAGS = -std=c17 -O2
CXXFLAGS = -std=c++17 -O2

FRAMEWORKS = \
	-framework CoreAudio \
	-framework AudioToolbox \
	-framework CoreGraphics \
	-framework CoreFoundation

TARGET = kbmod

SOURCES_CPP = main.cpp config.cpp
SOURCES_C = audio.c

OBJECTS = main.o config.o audio.o



Build: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(FRAMEWORKS) -o $(TARGET)

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

config.o: config.cpp
	$(CXX) $(CXXFLAGS) -c config.cpp -o config.o

audio.o: audio.c audio.h
	$(CC) $(CFLAGS) -c audio.c -o audio.o



Run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)
	
