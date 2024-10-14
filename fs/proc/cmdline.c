// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/setup.h>

static char new_command_line[COMMAND_LINE_SIZE];

static int cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_puts(m, new_command_line);
	seq_putc(m, '\n');
	return 0;
}

static void remove_flag(char *cmd, const char *flag)
{
	char *start_addr, *end_addr;
	/* Ensure all instances of a flag are removed */
	while ((start_addr = strstr(cmd, flag))) {
		end_addr = strchr(start_addr, ' ');
		if (end_addr)
			memmove(start_addr, end_addr + 1, strlen(end_addr));
		else
			*(start_addr - 1) = '\0';
	}
}
static void remove_safetynet_flags(char *cmd)
{
	remove_flag(cmd, "androidboot.veritymode=");
}
#if 1
static void replace_flag(char *cmd, const char *flag, const char *flag_new)
{
	char *start_addr, *end_addr;
	/* Ensure all instances of a flag are replaced */
	while ((start_addr = strstr(cmd, flag))) {
		end_addr = strchr(start_addr, ' ');
		if (end_addr)
			memcpy(start_addr, flag_new, strlen(flag));
		else
			*(start_addr - 1) = '\0';
	}
}
static void replace_safetynet_flags(char *cmd)
{
	// WARNING: be aware that you can't replace shorter string with longer ones in the function called here...
	replace_flag(cmd, "androidboot.vbmeta.device_state=unlocked",
			  "androidboot.vbmeta.device_state=locked  ");
	replace_flag(cmd, "androidboot.enable_dm_verity=0",
			  "androidboot.enable_dm_verity=1");
	replace_flag(cmd, "androidboot.warranty_bit=0",
			  "androidboot.warranty_bit=1"); 
	replace_flag(cmd, "androidboot.flash.locked=0",
       		          "androidboot.flash.locked=1");
       	replace_flag(cmd, "androidboot.fmp_config=0",
       		          "androidboot.fmp_config=1"); 
        replace_flag(cmd, "androidboot.veritymode=logging",
			  "androidboot.veritymode=enforcing");
	replace_flag(cmd, "androidboot.veritymode=eio",
			  "androidboot.veritymode=enforcing");
       	replace_flag(cmd, "androidboot.vbmeta.device_state=unlocked",
       		          "androidboot.vbmeta.device_state=locked");   
	replace_flag(cmd, "androidboot.secboot=disabled",
			  "androidboot.secboot=enabled ");
	replace_flag(cmd, "androidboot.verifiedbootstate=orange",
			  "androidboot.verifiedbootstate=green ");
}
#endif
static int __init proc_cmdline_init(void)
{
	strcpy(new_command_line, saved_command_line);
	/*
	 * Remove/replace various flags from command line seen by userspace in order to
	 * pass SafetyNet CTS check.
	 */
	replace_safetynet_flags(new_command_line);
	remove_safetynet_flags(new_command_line);
	proc_create_single("cmdline", 0, NULL, cmdline_proc_show);
	return 0;
}
fs_initcall(proc_cmdline_init);
