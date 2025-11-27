TARGET=flappy
ARGS=-o3 -Wall -Wpedantic
CXX=gcc

$(TARGET): flappy.c
	$(CXX) $(ARGS) -o $(TARGET) flappy.c

clean:
	rm $(TARGET)
