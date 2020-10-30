#include "internal.h"

#include <deps/cmd_arger.c>
#include <deps/utf8proc.c>
#include <errno.h>
#include <math.h>

#include "compiler.c"
#include "lexer.c"
#include "util.c"

int main(int argc, char** argv) {
	TekCompileArgs compile_args;

	CmdArgerDesc optional_args[] = {
	};
	CmdArgerDesc required_args[] = {
		cmd_arger_desc_string(&compile_args.file_path, "file_path", "path to the main source file you wish to compile"),
	};

	char* app_name_and_version = "tek compiler";
	cmd_arger_parse(
		optional_args, sizeof(optional_args) / sizeof(*optional_args),
		required_args, sizeof(required_args) / sizeof(*required_args),
		argc, argv, app_name_and_version);


	int threads_count = 1;
	/*
#ifdef __linux__
	threads_count = sysconf(_SC_NPROCESSORS_ONLN);
#else
#error "TODO implement for this platform"
#endif
*/

	TekCompiler c;
	TekCompiler_init(&c, threads_count);

	TekCompiler_compile_start(&c, &compile_args);
	TekCompiler_compile_wait(&c);

	if (TekCompiler_has_errors(&c)) {
		TekStk(char) error_string = {0};
		TekCompiler_errors_string(&c, &error_string, tek_true);
		printf("%.*s", error_string.count, error_string.TekStk_data);
	}

	return 0;
}

