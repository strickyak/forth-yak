BEGIN {
	print "// THIS IS GENERATED CODE."
}

/^=/ {
	print "  &&label_" $2 ","
}

END {
	print "// End."
}
