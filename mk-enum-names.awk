/^=/ {
	gsub(/"/, "\\\"", $3)  # Change " to \"
	print "  \"" $3 "\","
}
