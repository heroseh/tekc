#ifndef TEK_CONFIG_H
#define TEK_CONFIG_H

#define TEK_DEBUG_ASSERTIONS 0
#define TEK_HASH_64 0

#define tek_thread_sync_primitive_spin_iterations 128

#define TEK_DEBUG_TOKENS 1
#define TEK_DEBUG_SYNTAX_TREE 1
#define tek_debug_tokens_path "/tmp/tek_tokens"
#define tek_lexer_cap_open_brackets 128
#define tek_debug_syntax_tree_path "/tmp/tek_syntax_tree"

//===========================================================================================
//
//
// ANSI colors
//
//
//===========================================================================================

#define tek_ansi_color_fg_black "30"
#define tek_ansi_color_fg_red "31"
#define tek_ansi_color_fg_green "32"
#define tek_ansi_color_fg_yellow "33"
#define tek_ansi_color_fg_blue "34"
#define tek_ansi_color_fg_magenta "35"
#define tek_ansi_color_fg_cyan "36"
#define tek_ansi_color_fg_white "37"
#define tek_ansi_color_fg_black_bright "90"
#define tek_ansi_color_fg_red_bright "91"
#define tek_ansi_color_fg_green_bright "92"
#define tek_ansi_color_fg_yellow_bright "93"
#define tek_ansi_color_fg_blue_bright "94"
#define tek_ansi_color_fg_magenta_bright "95"
#define tek_ansi_color_fg_cyan_bright "96"
#define tek_ansi_color_fg_white_bright "97"

#define tek_error_color_file tek_ansi_color_fg_magenta_bright
#define tek_error_color_info tek_ansi_color_fg_white_bright
#define tek_error_color_error tek_ansi_color_fg_red_bright
#define tek_error_color_error_message tek_ansi_color_fg_yellow_bright
#define tek_error_color_arrows tek_ansi_color_fg_yellow_bright

#define tek_ansi_esc_color_fg_set(color) "\x1b["color"m"
#define tek_ansi_esc_color_reset(color) "\x1b[0m"

#endif
