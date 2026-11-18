CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDLIBS   = -lhiredis

tienda: tienda.cpp
	$(CXX) $(CXXFLAGS) tienda.cpp -o tienda $(LDLIBS)

clean:
	rm -f tienda
