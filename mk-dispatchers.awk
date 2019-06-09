BEGIN {
	once = 1
}

/^=/ {
	if (once == 0) {
		print "};  DISPATCH;"
	}
	print "label_" $2 ": {"
	once = 0
}

/^[^=]/ {
	print $0
}

END {
	print "};  DISPATCH;"
}
