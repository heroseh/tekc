#include <stdlib.h>
#include "deps/cmd_arger.h"
#include "deps/cmd_arger.c"

#define tekc_src_file "src/main.c"
#define tekc_out_file "build/tekc"

int main(int argc, char** argv) {
	CmdArgerBool debug = cmd_arger_false;
	CmdArgerBool debug_address = cmd_arger_false;
	CmdArgerBool debug_memory = cmd_arger_false;
	CmdArgerBool clean = cmd_arger_false;

	char* compiler = "clang";
	int64_t opt = 0;
	CmdArgerDesc desc[] = {
		cmd_arger_desc_flag(&debug, "debug", "compile in debuggable executable"),
		cmd_arger_desc_flag(&clean, "clean", "remove any built binaries"),
		cmd_arger_desc_flag(&debug_address, "debug_address", "turns on address sanitizer"),
		cmd_arger_desc_flag(&debug_memory, "debug_memory", "turns on address memory sanitizer"),
		cmd_arger_desc_string(&compiler, "compiler", "the compiler command"),
		cmd_arger_desc_integer(&opt, "opt", "compiler code optimization level, 0 none, 3 max"),
	};

	char* app_name_and_version = "tekc build script";
	cmd_arger_parse(desc, sizeof(desc) / sizeof(*desc), NULL, 0, argc, argv, app_name_and_version);

	int exe_res;
	if (clean) {
		exe_res = system("rm "tekc_out_file);
		if (exe_res != 0) { return exe_res; }
		return exe_res;
	}

#define buf_count 1024

	char buf[buf_count];
	char cflags[512];
	size_t cflags_idx = 0;
	cflags_idx += snprintf(cflags + cflags_idx, sizeof(cflags) - cflags_idx, "-O%zd -mstackrealign", opt);
	if (debug) {
		cflags_idx += snprintf(cflags + cflags_idx, sizeof(cflags) - cflags_idx, " -g3 -DDEBUG");
	}
	if (debug_address) {
		cflags_idx += snprintf(cflags + cflags_idx, sizeof(cflags) - cflags_idx, " -fsanitize=address");
	}
	if (debug_memory) {
		cflags_idx += snprintf(cflags + cflags_idx, sizeof(cflags) - cflags_idx, " -fsanitize=memory");
	}
	cflags_idx += snprintf(cflags + cflags_idx, sizeof(cflags) - cflags_idx, " -pthread -lm");
	char* env_cflags = getenv("CFLAGS");
	if (env_cflags == NULL) {
		env_cflags = "";
	}

	char* include_paths = "-I./";

	// ensure the build directory exists
	exe_res = system("mkdir -p build");
	if (exe_res != 0) { return exe_res; }

	// compile tekc
	snprintf(buf, buf_count, "%s %s %s -o %s %s %s", compiler, env_cflags, cflags, tekc_out_file, tekc_src_file, include_paths);
	exe_res = system(buf);
	if (exe_res != 0) { return exe_res; }

	return exe_res;
}

