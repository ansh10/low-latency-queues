CXX      := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -pthread
LDFLAGS  := -pthread

BUILD    := build
TARGET   := $(BUILD)/spsc_queue
SRCS     := *.cpp
OBJS     := $(SRCS:%.cpp=$(BUILD)/%.o)
DEPS     := $(OBJS:.o=.d)

.PHONY: all clean run debug test tsan

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

debug: CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -pthread
debug: LDFLAGS  := -fsanitize=address,undefined -pthread
debug: clean all

tsan: CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -g -fsanitize=thread -pthread
tsan: LDFLAGS  := -fsanitize=thread -pthread
tsan: clean all

run: $(TARGET)
	./$(TARGET)

test: run

clean:
	rm -rf $(BUILD)

-include $(DEPS)
