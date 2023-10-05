#!/usr/bin/env -S awk -f

BEGIN {
	libname="unknown"
	export_dir="./env/docs/"

	print "Libraries (access library index by running `docs \"[LIBNAME]\"):" > export_dir "index.bdoc"
}

/\/\*@\$/,/\$@\*\// {
	if( $1 ~ /libname/ ) {
		libname=$2
		print libname > export_dir libname ".bdoc"
		printf "\t-%s\n", libname >> export_dir "index.bdoc"
	}
}


/\/\*@@/,/@@\*\// {
	if($0 ~ /.*@@.*/) {
		linecounter = 0
		docname = "unkown"
		next
	}
	linecounter += 1
	gsub(/^\t/, "", $0) # Strip the leading tab character
	
	if(linecounter == 1) {
		docname = $0
		printf "%s (%s)\n", docname, libname > export_dir docname ".bdoc"
		printf "\t-%s\n", docname >> export_dir libname ".bdoc"
	} else if(linecounter == 2 && $0) {
		printf "\t%s\n", $0 >> export_dir docname ".bdoc"
	} else if(linecounter > 2) {
		print $0 >> export_dir docname ".bdoc"
	}
}

END {
	com = sprintf("cp %s %s", export_dir "index.bdoc", export_dir "help.bdoc")
	system(com)
}
