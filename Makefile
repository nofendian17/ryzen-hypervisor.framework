# Top-level build. Run on the Hackintosh (needs Xcode command line tools).
#
#   make test     host-side unit tests, works on any OS with a C compiler
#   make tools    build/amd-info, build/hvf-smoke
#   make kext     build/AMDHV.kext   (see kext/Makefile)
#   make all      tools + kext
#   make clean

BUILD := build
CC    ?= cc
CXX   ?= c++

.PHONY: all test tools kext clean

all: tools kext

$(BUILD):
	mkdir -p $(BUILD)

test: | $(BUILD)
	$(CC) -Wall -Wextra -Werror -o $(BUILD)/host_test tests/host_test.c
	$(BUILD)/host_test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -o $(BUILD)/vmcb_layout tests/vmcb_layout.cpp
	$(BUILD)/vmcb_layout
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o $(BUILD)/npt_test tests/npt_test.cpp
	$(BUILD)/npt_test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o $(BUILD)/vmx_map_test tests/vmx_map_test.cpp
	$(BUILD)/vmx_map_test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o $(BUILD)/vmcs_state_test tests/vmcs_state_test.cpp
	$(BUILD)/vmcs_state_test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -mavx2 -fsanitize=address,undefined -g -o $(BUILD)/fpu_state_test tests/fpu_state_test.cpp
	$(BUILD)/fpu_state_test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o $(BUILD)/msr_map_test tests/msr_map_test.cpp
	$(BUILD)/msr_map_test

tools: | $(BUILD)
	$(CC) -arch x86_64 -Wall -Wextra -O2 -o $(BUILD)/amd-info tools/amd-info/main.c \
		-framework IOKit -framework CoreFoundation
	$(CC) -arch x86_64 -Wall -Wextra -Wno-nullability-completeness -O2 -o $(BUILD)/hvf-smoke tools/hvf-smoke/main.c \
		-framework Hypervisor

kext:
	$(MAKE) -C kext

clean:
	rm -rf $(BUILD)
