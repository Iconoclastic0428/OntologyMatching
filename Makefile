# Compiler
CXX = /usr/bin/g++

# Compiler flags
CXXFLAGS = -fdiagnostics-color=always -g

# Source files
SRC = ./main.cpp

# Output binary
OUT = ./EntityMatching

# Libraries
LIBS = -lcrypto -ltbb

all: $(OUT)

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT) $(LIBS)

clean:
	rm -f $(OUT)
