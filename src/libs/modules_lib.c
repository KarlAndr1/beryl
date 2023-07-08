#include "../interpreter.h"
#include "lib_utils.h"

void modules_lib_load() {
	DEF_VAR("loaded-libraries", invoke table);
	DEF_FN("require", path,
		if path in? loaded-libraries do \n
			loaded-libraries path \n
		end, else do \n
			let global script-path = path \n
			let res = eval (read path) \n
			loaded-libraries = loaded-libraries union: (table path res) \n
			res
		end
	);
	DEF_FN("require-lib", path,
		require (cat+ lib-dir path-separator path)
	);
}
