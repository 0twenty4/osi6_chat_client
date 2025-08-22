CXX := g++

CXXFLAGS := -std=c++2a -Wall -pthread 
LDFLAGS := -lncursesw -lboost_locale

# Исходники
SRCS := client.cpp

# Имя исполняемого файла
TARGET := client

# Сборка
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

# Очистка
clean:
	rm -f $(TARGET)
