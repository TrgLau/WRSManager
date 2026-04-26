-include makefile.config

ifeq ($(OS),Windows_NT)
  EXEEXT ?= .exe
else
  EXEEXT ?=
endif

APP      ?= WRSManager
BINDIR   ?= bin
BUILDDIR ?= build

CXX       ?= g++
WX_CONFIG ?= wx-config

WX_CXXFLAGS := $(shell $(WX_CONFIG) --cxxflags 2>/dev/null)
WX_LIBS     := $(shell $(WX_CONFIG) --libs 2>/dev/null) $(shell $(WX_CONFIG) --libs richtext 2>/dev/null)
ifeq ($(OS),Windows_NT)
  SYS_LIBS := -lwininet
else
  SYS_LIBS :=
endif

ifeq ($(strip $(WX_CXXFLAGS)),)
  $(error wx-config introuvable ou inutilisable. Definissez WX_CONFIG, ou installez wxWidgets \
    (ex. MSYS2 : pacman -S mingw-w64-x86_64-wxwidgets3.2-msw) et lancez make depuis le shell MinGW64.)
endif

INCLUDES := -Iinclude
CPPFLAGS :=
CXXFLAGS_EXTRA ?= -Wall -Wextra -std=c++17
CXXFLAGS := $(WX_CXXFLAGS) $(INCLUDES) $(CXXFLAGS_EXTRA) -MMD -MP

SRC := src/main.cpp src/App.cpp src/MainFrame.cpp src/TerminalLogger.cpp
OBJ := $(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

TARGET := $(BINDIR)/$(APP)$(EXEEXT)

.PHONY: all clean run dirs

all: dirs $(TARGET)

dirs: $(BINDIR) $(BUILDDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJ) | $(BINDIR)
	$(CXX) -o $@ $(OBJ) $(WX_LIBS) $(SYS_LIBS)

$(BUILDDIR)/%.o: src/%.cpp | $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) -r $(BUILDDIR) $(BINDIR)

run: all
	./$(TARGET)

-include $(DEP)
