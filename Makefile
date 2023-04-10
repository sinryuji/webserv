#---------------[ color ]---------------
COMPILE_COLOR	=	"\033[1;34m"
BUILD_COLOR		=	"\033[1;32m"
MODE_COLOR		=	"\033[1;35m"
DEL_COLOR	    =	"\033[1;31m"
END				    =	"\033[0;0m"
CLEAR			    =	"\x1b[1A\x1b[M"

#---------------[ webserv ]-------------
NAME			=	webserv

INCS			=	./src

SRCS_DIR		=	./src
SRCS			=	main.cpp\
						Server.cpp\
						Socket.cpp


OBJS_DIR	=	./obj
OBJS			=	$(addprefix $(OBJS_DIR)/, $(SRCS:.cpp=.o))
CXX       =	c++
CPPFLAGS  =	-I $(INCS)
#CXXFLAGS	=	-Wall -Wextra -Werror -std=c++98
RM				=	rm -rf

vpath %.cpp $(SRCS_DIR)

all: $(NAME)

$(OBJS_DIR)/%.o: %.cpp
	@echo $(COMPILE_COLOR) "💾 [$<] Compiling..." $(END)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo $(COMPILE_COLOR) "✨ [$@] was created" $(END)

$(NAME): $(OBJS_DIR) $(OBJS)
	@echo $(BUILD_COLOR) "🔨 [$@] Building..." $(END)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OBJS) -o $(NAME)
	@echo $(BUILD_COLOR) "🎉 [$@] was created" $(END)

clean:
	@$(RM) $(OBJS) $(OBJS_DIR)
	@echo $(DEL_COLOR) "🧹 [Object files] were removed" $(END)

fclean: clean
	@$(RM) $(NAME)
	@echo $(DEL_COLOR) "🧹 [$(NAME)] were removed" $(END)

re:
	@make fclean
	@make all

$(OBJS_DIR):
	@mkdir $(OBJS_DIR)

.PHONY: all clean fclean re
