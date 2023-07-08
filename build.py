import os
import subprocess
import sys

def get_all_in(path, extensions):
	out_set = set()
	
	for root, dirs, files in os.walk(path):
		for f in files:
			for ext in extensions:
				if f.endswith(ext):
					full_file_path = os.path.join(root, f)
					assert(full_file_path not in out_set)
					out_set.add(full_file_path)
					break
	
	return out_set;

def build_all_except(cc, src_path, exclude, extensions, flags, out):
	actual_excludes = set()
	for excl in exclude:
		actual_excludes.add(os.path.join(src_path, excl))
	exclude = actual_excludes
	
	src_files = get_all_in(src_path, extensions)
	
	args = [cc]
	args += flags
	if out:
		args.append("-o" + out)
	
	for src_f in src_files:
		if src_f not in exclude:
			args.append(src_f)
	
	return args;


def print_command(command):
	for arg in command:
		print(arg, end=" ")
	print()

def build_release():
	commands = build_all_except(
		compiler, 
		"src", 
		set(), 
		extensions, 
		["-DNO_INCLUDE_ASSERTS", "-DNO_TESTS", "-O2", "-std=c99"], 
		"out/interpreter.out"
	)
	commands += ["-lm"]
	print_command(commands)
	subprocess.run(commands)
	
def build_debug():
	commands = build_all_except(
		compiler, 
		"src", 
		set(), 
		extensions, 
		["-DDEBUG", "-Wall", "-Wpedantic", "-g", "-std=c99", "-fsanitize=address", "-fsanitize=undefined", "-fsanitize=leak"], 
		"out/a.out"
	)
	commands += ["-lm"]
	print_command(commands)
	subprocess.run(commands)


def build_lib():
	os.chdir("out")
	commands = build_all_except(
		compiler,
		"../src",
		{ "main.c" },
		extensions,
		["-O2", "-std=c99", "-c"],
		None
	)
	commands += ["-lm"]
	print_command(commands)
	subprocess.run(commands)
	
	lib_files = list(get_all_in("./", [".o"]))
	commands = ["ar", "rcs", "lib.ar"] + lib_files
	print_command(commands)
	subprocess.run(commands)
	subprocess.run(["cp", "-t", "./", "../src/interpreter.h"])

def build_minimal_lib():
	os.chdir("out")
	commands = build_all_except(
		compiler,
		"../src",
		{ 
			"main.c", 
			"io.c",
			"libs/datastructures_lib.c", 
			"libs/io_lib.c", 
			"libs/debug_lib.c", 
			"libs/common_lib.c", 
			"libs/math_lib.c", 
			"libs/string_lib.c",
			"libs/modules_lib.c"
		},
		extensions,
		["-O2", "-std=c99", "-c", "-DMINIMAL_BUILD"],
		None
	)
	print_command(commands)
	subprocess.run(commands)
	
	lib_files = list(get_all_in("./", [".o"]))
	commands = ["ar", "rcs", "lib.ar"] + lib_files
	print_command(commands)
	subprocess.run(commands)
	subprocess.run(["cp", "-t", "./", "../src/interpreter.h"])

compiler = "cc"
extensions = [".c"]

if len(sys.argv) == 2 and sys.argv[1] == "d":
	build_debug()
else:
	while True:
		print("Select build option:")
		print("1. Standalone (Interpreter + Libraries + REPL)")
		print("2. Debug")
		print("3. Library")
		print("4. Minimal library (no datastructures or IO functions)")
		print("Q. Quit")
		i = input(":")
		if i == "1":
			build_release()
			break
		elif i == "2":
			build_debug()
			break
		elif i == "3":
			build_lib()
			break
		elif i == "4":
			build_minimal_lib()
			break
		elif i.lower() in ("q", "quit"):
			break
		
