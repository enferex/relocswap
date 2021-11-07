APP=relocswap
CXXFLAGS=--std=c++17 --pedantic -Wall $(EXTRA_CXXFLAGS)
SOURCES=main.cc
OBJS=$(SOURCES:.cc=.o)

all: debug

debug: CXXFLAGS+=-g3 -O0
debug: $(APP)

release: CXXFLAGS+=-O3
release: $(APP)

$(APP): $(OBJS)
	$(CXX) -o $@ $^

clean:
	$(RM) $(APP) $(OBJS)
