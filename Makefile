NAME		:= ircserv
TEST_NAME	:= run_tests

CXX			:= c++
CXXFLAGS	:= -Wall -Wextra -Werror -std=c++98 -MMD -MP
INCLUDES	:= -I include

SRC_DIR		:= src
OBJ_DIR		:= obj
TEST_DIR	:= tests

# Wildcard on purpose: two devs add .cpp files in parallel on separate
# branches, and an explicit list would conflict on every merge.
SRCS		:= $(wildcard $(SRC_DIR)/*.cpp)
OBJS		:= $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

TEST_SRCS	:= $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJS	:= $(TEST_SRCS:$(TEST_DIR)/%.cpp=$(OBJ_DIR)/$(TEST_DIR)/%.o)
# The unit tests link every object except main.o, which has its own main().
LIB_OBJS	:= $(filter-out $(OBJ_DIR)/main.o, $(OBJS))

DEPS		:= $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/$(TEST_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Inner testing loop: builds and runs the constructed-string unit tests.
# Not part of `all`, so the graded build never depends on the test files.
test: $(TEST_NAME)
	./$(TEST_NAME)

$(TEST_NAME): $(LIB_OBJS) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LIB_OBJS) $(TEST_OBJS) -o $(TEST_NAME)

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME) $(TEST_NAME)

re: fclean all

# Header changes trigger recompilation via the .d files gcc/clang generate,
# which is what keeps `make` from relinking (or under-building) needlessly.
-include $(DEPS)

.PHONY: all clean fclean re test
