CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LIBS     = -llgpio -lpthread

TARGET = doggycart
SRC    = main.cpp

all: check-deps $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGET)

# ── Dependency checks ────────────────────────────────────────

check-deps:
	@test -f httplib.h || { \
	    echo ""; \
	    echo "ERROR: httplib.h not found."; \
	    echo "  Download the single-header release from the cpp-httplib project"; \
	    echo "  (search: cpp-httplib yhirose) and place httplib.h here."; \
	    echo "  Or: sudo apt install libcpp-httplib-dev  (if available)"; \
	    echo ""; \
	    exit 1; \
	}
	@pkg-config --exists lgpio 2>/dev/null || dpkg -l liblgpio-dev 2>/dev/null | grep -q '^ii' || { \
	    echo ""; \
	    echo "ERROR: lgpio not found. Run: sudo apt install liblgpio-dev"; \
	    echo ""; \
	    exit 1; \
	}

# ── Install helper ───────────────────────────────────────────

install-deps:
	sudo apt install -y liblgpio-dev
	@echo ""
	@echo "Still needed: download httplib.h from the cpp-httplib project"
	@echo "and place it in this directory."

.PHONY: all clean check-deps install-deps
