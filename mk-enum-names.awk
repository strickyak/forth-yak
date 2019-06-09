/^=/ {
	gsub(/\\/, "\\\\", $3)  # Change \ to \\
	gsub(/"/, "\\\"", $3)  # Change " to \"
	print "  \"" $3 "\","
}
