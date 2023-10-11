#include "../beryl.h"

#include "../utils.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#if defined(__unix__)
	#include <dlfcn.h>
	#include <unistd.h>
	#include <sys/wait.h>
	#include <regex.h>
	typedef void *dyn_lib;
	#define DL_IS_NULL(dl) ((dl) == NULL)
	#define DL_OPEN(path) dlopen(path, RTLD_NOW)
	#define DL_PROC(dl, name) dlsym(dl, name)
	
	#define PLATFORM "unix"
#elif defined(_WIN32)
	#include <windows.h>
	#include <process.h>
	typedef HINSTANCE dyn_lib;
	#define DL_IS_NULL(dl) ((dl) == NULL)
	#define DL_OPEN(path) LoadLibraryA(path)
	#define DL_PROC(dl, name) GetProcAddress(dl, name)
	
	#define PLATFORM "windows"
#else
	typedef void *dyn_lib;
	#define DL_IS_NULL(dl) ((dl) == NULL)
	#define DL_OPEN(path) (NULL)
	#define DL_PROC(dl, name) (NULL)
	
	#define PLATFORM "other"
#endif


typedef struct i_val i_val;

/*@$
	libname unixlib
$@*/

/*@@
	load-dl
	path

	Unary function.
	Attempts to load the dynamic library found at *path* as a berylscript library.
	Returns the contents of that library (Generally a table of functions and constants).
	Returns an error if out of memory, if the library does not exist or if it is not a valid berylscript library.
	If load-dl is not supported, attempting to open a dynamic library returns an "Unable to open dynamic library" error.
@@*/
static i_val load_dl_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected string path as first argument");
	}
	i_size path_len = BERYL_LENOF(args[0]);
	char *c_path = beryl_talloc(path_len + 1);
	if(c_path == NULL)
		return BERYL_ERR("Out of memory");
	c_path[path_len] = '\0';
	memcpy(c_path, beryl_get_raw_str((i_val *) &args[0]), path_len);
	
	dyn_lib dl = DL_OPEN(c_path);
	beryl_tfree(c_path);
	
	if(DL_IS_NULL(dl)) {
		beryl_blame_arg(args[0]);
		#ifdef __unix__
		const char *err_msg = dlerror();
		#else
		const char *err_msg = NULL;
		#endif
		if(err_msg != NULL) {
			i_val err_str = beryl_new_string(strlen(err_msg), err_msg);
			beryl_blame_arg(err_str);
			beryl_release(err_str);
		}
		return BERYL_ERR("Unable to open dynamic library");
	}
	
	void *lib_load = DL_PROC(dl, "beryl_lib_load");
	if(lib_load == NULL) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Not a BerylScript library");
	}
	i_val (*lib_load_fn_ptr)() = (i_val (*)()) lib_load;
	
	return lib_load_fn_ptr();
}

/*@@
	getenv
	var-name
	
	Unary function.
	Retrieves the value of the environment variable named *var-name*, as a string.
	If the variable does not exist, null is returned.
	Returns an error if out of memory.
@@*/
static i_val getenv_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected string as argument for 'getenv'");
	}
	
	i_size len = BERYL_LENOF(args[0]);
	char *c_name = beryl_talloc(len + 1);
	if(c_name == NULL)
		return BERYL_ERR("Out of memory");
	
	memcpy(c_name, beryl_get_raw_str(&args[0]), len);
	c_name[len] = '\0';
	
	const char *env_var = getenv(c_name);
	beryl_tfree(c_name);
	
	if(env_var == NULL)
		return BERYL_NULL;
	
	i_val res = beryl_new_string(strlen(env_var), env_var);
	if(BERYL_TYPEOF(res) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	return res;
}

#ifdef __unix__

static void close_fd_pair(int p[2]) {
	close(p[0]);
	close(p[1]);
}

#endif

static int p_spawn(const char *cmd, char **argv, int *return_code, const i_val *pass) { // return of 0 means sucess, -1 some system error (check errno), -2 unsupported platform, -3 *passing input* is not supported, -4 unable to run process
	#if defined(__unix__)
	
	int host_to_sub[2];
	int perr = pipe(host_to_sub);
	if(perr)
		return -1;


	pid_t pid = fork();
	if(pid == -1) {
		close_fd_pair(host_to_sub);
		return -1;
	}

	if(pid == 0) { //In child process
		close(host_to_sub[1]);
		if(pass != NULL)
			dup2(host_to_sub[0], STDIN_FILENO); //Set STDIN to get the input from the pipe that the host writes to.
		close(host_to_sub[0]);
		execvp(cmd, argv);
		exit(127); //If exec failed
	} else { //In the parent process
		close(host_to_sub[0]);
		if(pass != NULL) {
			assert(BERYL_TYPEOF(*pass) == TYPE_STR);
			ssize_t n = write(host_to_sub[1], beryl_get_raw_str(pass), BERYL_LENOF(*pass));
			if(n == -1) {
				//An error occured when writing to the subprocess... what do we do here exactly?				
			}
		}
		close(host_to_sub[1]);
		
		int wstatus;
		wait(&wstatus);
		if(WIFEXITED(wstatus)) {
			*return_code = WEXITSTATUS(wstatus);
			if(*return_code == 127)
				return -4;
		} else
			*return_code = -1;
		return 0;
	}
	
	
	#elif defined(_WIN32)
	if(pass != NULL)
		return -3;
	int res = _spawnvp(_P_WAIT, cmd, (const char * const *) argv);
	if(res == -1)
		return -1;
	
	*return_code = res;
	return 0;
	#else
	return -2;
	#endif
}

static char **convert_strs_to_cstrs(const i_val *strs, size_t n_strs, bool null_end) {
	char **array;
	if(null_end) {
		array = beryl_talloc((n_strs + 1) * sizeof(char *));
		if(array != NULL)
			array[n_strs] = NULL;
	} else
		array = beryl_talloc(n_strs * sizeof(char *));
	
	if(array == NULL)
		return NULL;
	
	for(size_t i = 0; i < n_strs; i++) {
		const char *str_src = beryl_get_raw_str(&strs[i]);
		size_t len = BERYL_LENOF(strs[i]);
		char *cstr = beryl_alloc(len + 1);
		if(cstr == NULL) {
			for(ssize_t j = (ssize_t) i - 1; j >= 0; j--) {
				beryl_free(array[j]);
			}
			beryl_tfree(array);
			return NULL;
		}
		memcpy(cstr, str_src, len);
		cstr[len] = '\0';
		array[i] = cstr;
	}
	
	return array;
}


static i_val pass_input_tag;

/*@@
	run
	cmd ... args [pass-input input-str]

	Variadic function taking at least one argument.
	Runs the application with the name given by the string *cmd* with the string arguments *args*.
	The interpreter waits for the application to exit.
	Returns the exit code of the application as a number.
	Returns an error if out of memory, if an internal error occurs, or if run is not supported.
	
	Optionally, the input fed into the new processes' stdin can be set via pass-input and *input-str*.
	Example:
		run "grep" "hello" pass-input "hello world"
@@*/
static i_val run_callback(const i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected command (string) as first argument for 'run'");
	}
	const i_val *pass = NULL;
	for(i_size i = 1; i < n_args; i++) { 
		if(beryl_val_cmp(args[i], pass_input_tag) == 0) {
			if(i + 1 != n_args - 1) {
				printf("%u, %u\n", (unsigned) i, (unsigned) n_args);
				return BERYL_ERR("'pass-input' must be followed by exactly one argument");
			}
			if(BERYL_TYPEOF(args[i+1]) != TYPE_STR) {
				beryl_blame_arg(args[i+1]);
				return BERYL_ERR("Can only pass strings to processes");
			}
			pass = &args[n_args - 1];
			assert(n_args > 2);
			n_args -= 2;
			break;
		}
		if(BERYL_TYPEOF(args[i]) != TYPE_STR) {
			beryl_blame_arg(args[i]);
			return BERYL_ERR("Expected string as argument for 'run'");
		}
	}
	
	char **cmd = convert_strs_to_cstrs(args, n_args, true);
	if(cmd == NULL)
		return BERYL_ERR("Out of memory");
	
	int return_code = 0;
	int res = p_spawn(cmd[0], cmd, &return_code, pass);
	for(size_t i = 0; i < n_args; i++)
		beryl_free(cmd[i]);
	beryl_tfree(cmd);
	
	switch(res) {
		case -1: {
			const char *cerr = strerror(errno);
			i_val err_msg = beryl_new_string(strlen(cerr), cerr);
			if(BERYL_TYPEOF(err_msg) == TYPE_NULL)
				return BERYL_ERR("Cannot display system error; Out of memory");
			else
				return beryl_str_as_err(err_msg);
		}
		
		case -2:
			return BERYL_ERR("Run is not supported on this platform");
		
		case -3:
			return BERYL_ERR("'pass-input' is not supported on this platform");
		
		case -4:
			beryl_blame_arg(args[0]);
			return BERYL_ERR("Unable to run program");
	}
	
	return BERYL_NUMBER(return_code);
}

static i_val random_i_string(i_size len) {
	#if defined(__unix__)

	i_val str_val = beryl_new_string(len, NULL);
	if(BERYL_TYPEOF(str_val) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	char *str = (char *) beryl_get_raw_str(&str_val);
	
	FILE *f = fopen("/dev/urandom", "r");
	if(!f) {
		beryl_release(str_val);
		return BERYL_ERR("Internal error; Unable to open /dev/urandom");
	}
	
	size_t read_len = fread(str, sizeof(char), len, f);
	fclose(f);
	
	if(read_len != len) {
		beryl_release(str_val);
		return BERYL_ERR("Internal error; Unable to read from /dev/urandom");
	}
	
	return str_val;
	
	#else
	(void) len;
	return BERYL_ERR("'rands' is not supported on this platform");
	
	#endif
}

/*@@
	rands
	length

	Generates a random string of bytes of the given *length*.
	On UNIX this is done via /dev/urandom
@@*/
static i_val rands_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(!beryl_is_integer(args[0])) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected integer length as argument");
	}

	i_float len = beryl_as_num(args[0]);
	if(len < 0 || len > I_SIZE_MAX) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Random string length is out of range");
	}
	
	return random_i_string((i_size) len);
}

/*@@
	rand-hexs
	length

	Returns a string of the given *length* containing random numbers in hexadecimal notation (0-9, a-z).
	On UNIX this id one via /dev/urandom
@@*/
static i_val rands_hex(const i_val *args, i_size n_args) {
	(void) n_args;
	if(!beryl_is_integer(args[0])) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected integer length as argument");
	}

	i_float len = beryl_as_num(args[0]);
	if(len < 0 || len > I_SIZE_MAX) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Random string length is out of range");
	}
	
	i_size ilen = len;
	i_val str = random_i_string(ilen);
	if(BERYL_TYPEOF(str) == TYPE_ERR)
		return str;

	char *str_bytes = (char *) beryl_get_raw_str(&str);
	for(i_size i = 0; i < ilen; i++) {
		unsigned char *byte = (unsigned char *) &str_bytes[i];
		unsigned char hex_val = *byte % 16;
		
		char hex_char = hex_val < 10 ? '0' + hex_val : 'a' + (hex_val - 10);
		str_bytes[i] = hex_char;
	}
	
	return str;
}

/*@@
	time

	Zero-arity function.
	Returns the current time in seconds since 1 January 1970, 00:00 (UTC), also known
	as unix time.
@@*/
static i_val time_callback(const i_val *args, i_size n_args) {
	(void) args, (void) n_args;
	time_t t = time(NULL);
	if(t == -1) {
		return BERYL_ERR("Time error");
	}
	return BERYL_NUMBER(t);
}

/*@@
	convert-time
	unix-time

	Unary function.
	Converts a unix-timestamp into a structure containing the following members:
		second
		minute
		hour
		day
		month
		year
		day-of-the-year
	The time is based off of the local time.
	
	Example:
		let t = invoke time
		let ts = convert-time t
		print "Today is the" (ts :day) "of the month"
@@*/
static i_val convert_time_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected number as argument for 'format-time'");
	}
	
	time_t t = beryl_as_num(args[0]);
	struct tm *time = localtime(&t);
	
	i_val time_obj = beryl_new_table(9, true);
	if(BERYL_TYPEOF(time_obj) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	beryl_table_insert(&time_obj, BERYL_CONST_STR("second"), BERYL_NUMBER(time->tm_sec), false);
	beryl_table_insert(&time_obj, BERYL_CONST_STR("minute"), BERYL_NUMBER(time->tm_min), false);
	beryl_table_insert(&time_obj, BERYL_CONST_STR("hour"), BERYL_NUMBER(time->tm_hour), false);
	beryl_table_insert(&time_obj, BERYL_CONST_STR("day"), BERYL_NUMBER(time->tm_mday), false);
	beryl_table_insert(&time_obj, BERYL_CONST_STR("month"), BERYL_NUMBER(time->tm_mon), false);
	beryl_table_insert(&time_obj, BERYL_CONST_STR("year"), BERYL_NUMBER(1900 + time->tm_year), false);
	beryl_table_insert(&time_obj, BERYL_CONST_STR("day-of-the-year"), BERYL_NUMBER(time->tm_yday), false);
	beryl_table_insert(&time_obj, BERYL_CONST_STR("daylight-savings"), BERYL_BOOL(time->tm_isdst), false);
	
	int weekday = time->tm_wday == 0 ? 6 : time->tm_wday - 1;
	beryl_table_insert(&time_obj, BERYL_CONST_STR("weekday"), BERYL_NUMBER(weekday), false); 
	
	return time_obj;
}

/*@
	get-time
	time-struct

	Unary function.
	Takes a struct (or a function) like the one returned by convert-time and returns the
	unix-timestamp, as a number, for that time.
	The struct must contain the following entries:
	:second (number)
	:minute (number)
	:hour (number)
	:day (number)
	:month (number)
	:year (number)
	:daylight-savings (boolean)
@*/
static i_val get_time_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	
	#define REQ_NUM(name, strname) \
		i_val name = beryl_call(args[0], &BERYL_CONST_STR(strname), 1, true); \
		if(BERYL_TYPEOF(name) == TYPE_ERR) \
			return name; \
		if(BERYL_TYPEOF(name) != TYPE_NUMBER || !beryl_is_integer(name)) { \
			beryl_blame_arg(args[0]); \
			beryl_blame_arg(name); \
			beryl_release(name); \
			return BERYL_ERR("Expected integer number given '" #strname "' for %0, got %1"); \
		}
	
	REQ_NUM(second, "second");
	REQ_NUM(minute, "minute");
	REQ_NUM(hour, "hour");
	REQ_NUM(day, "day");
	REQ_NUM(month, "month");
	REQ_NUM(year, "year");
	//REQ_NUM(day_of_the_year, "day-of-the-year");
	i_val daylight_savings = beryl_call(args[0], &BERYL_CONST_STR("daylight-savings"), 1, true);
	if(BERYL_TYPEOF(daylight_savings) == TYPE_ERR)
		return daylight_savings;
	if(BERYL_TYPEOF(daylight_savings) != TYPE_BOOL) {
		beryl_blame_arg(args[0]);
		beryl_blame_arg(args[1]);
		beryl_release(daylight_savings);
		return BERYL_ERR("Expected boolean given 'daylight-savings' for %0, got %1");
	}
	
	struct tm ts = {
		.tm_sec = beryl_as_num(second),
		.tm_min = beryl_as_num(minute),
		.tm_hour = beryl_as_num(hour),
		.tm_mday = beryl_as_num(day),
		.tm_mon = beryl_as_num(month),
		.tm_year = beryl_as_num(year) - 1900,
		.tm_isdst = beryl_as_bool(daylight_savings)
	};
	
	time_t t = mktime(&ts);
	if(t == -1)	{
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Invalid time, %0");
	}
	return BERYL_NUMBER(t);
}

/*@
	regex
	expr str

	Binary function.
	Matches the string *str* against the POSIX regular expression *expr* and returns an array.
	The first index of the array is the matched string, the rest of the array contains the strings
	captured as capture groups (if any).
	Returns an empty array if the expression is not matched.
	May return an error on out of memory or if the expression is invalid.
@*/
static i_val regex_callback(const i_val *args, i_size n_args) {
	(void) n_args, (void) args;
	
	#if defined(__unix__)
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected string (pattern) as first argument for 'regex'");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_STR) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected string as second argument for 'regex'");
	}
	
	size_t plen = BERYL_LENOF(args[0]);
	size_t mlen = BERYL_LENOF(args[1]);
	
	char *buff = beryl_talloc(plen + 1 + mlen + 1);
	if(buff == NULL)
		return BERYL_ERR("Out of memory");
	
	memcpy(buff, beryl_get_raw_str(&args[0]), plen);
	buff[plen] = '\0';
	
	memcpy(buff + plen + 1, beryl_get_raw_str(&args[1]), mlen);
	buff[plen + 1 + mlen] = '\0';
	
	regex_t reg;
	if(regcomp(&reg, buff, 0)) {
		beryl_tfree(buff);
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Regex error");
	}
	
	#define MAX_MATCH 16
	static regmatch_t matches[MAX_MATCH];
	int res = regexec(&reg, buff + plen + 1, MAX_MATCH, matches, 0);
	if(res != 0)
		matches[0] = (regmatch_t) { -1, -1 };
	
	regfree(&reg);
	beryl_tfree(buff);
	
	const char *match_str = beryl_get_raw_str(&args[1]);
	i_val res_array = beryl_new_array(0, NULL, 1, false);
	if(BERYL_TYPEOF(res_array) == TYPE_ERR)
		return BERYL_ERR("Out of memory");
	
	for(size_t i = 0; i < MAX_MATCH; i++) {
		if(matches[i].rm_so == -1)
			break;
		size_t match_end = matches[0].rm_so + matches[i].rm_eo;
		i_val str = beryl_new_string(match_end - matches[i].rm_so, match_str + matches[i].rm_so);
		if(BERYL_TYPEOF(str) == TYPE_ERR) {
			beryl_release(res_array);
			return BERYL_ERR("Out of memory");
		}
		
		bool ok = beryl_array_push(&res_array, str);
		beryl_release(str);
		if(!ok) {
			beryl_release(res_array);
			return BERYL_ERR("Out of memory");
		}
	}
	
	return res_array;
	
	#undef MAX_MATCH
	
	beryl_tfree(buff);
	
	#else
	
	return BERYL_ERR("'regex' is not supported on this platform");
	
	#endif
}

#define FN(arity, name, fn) { arity, true, name, sizeof(name) - 1, fn }

bool load_unix_lib() {
	static struct beryl_external_fn fns[] = {
		FN(1, "load-dl", load_dl_callback),
		FN(1, "getenv", getenv_callback),
		FN(-2, "run", run_callback),
		FN(1, "rands", rands_callback),
		FN(1, "rand-hexs", rands_hex),
		FN(0, "time", time_callback),
		FN(1, "convert-time", convert_time_callback),
		FN(1, "get-time", get_time_callback),
		FN(2, "regex", regex_callback)
	};

	for(size_t i = 0; i < LENOF(fns); i++) {
		bool ok = beryl_set_var(fns[i].name, fns[i].name_len, BERYL_EXT_FN(&fns[i]), true);
		if(!ok)
			return false;
	}
	
	pass_input_tag = beryl_new_tag();
	beryl_set_var("pass-input", sizeof("pass-input") - 1, pass_input_tag, true);
	
	beryl_set_var("platform", sizeof("platform") - 1, BERYL_CONST_STR(PLATFORM), true);
	
	return true;
}

