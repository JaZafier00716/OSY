


all: calcpipe gen filter transform sum

clean:
	rm -f calcpipe gen filter transform sum

gen: gen.cpp
	$(CXX) $(CXXFLAGS) -o gen gen.cpp

calcpipe: calcpipe.cpp
	$(CXX) $(CXXFLAGS) -o calcpipe calcpipe.cpp

filter: filter.cpp
	$(CXX) $(CXXFLAGS) -o filter filter.cpp

transform: transform.cpp
	$(CXX) $(CXXFLAGS) -o transform transform.cpp

sum: sum.cpp
	$(CXX) $(CXXFLAGS) -o sum sum.cpp
