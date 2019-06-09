/^=/ {
	flags = "0"
	if ( $1 == "=i" ) {
		flags = flags "|IMMEDIATE_BIT"
	}
        gsub(/"/, "\\\"", $3)

	printf "  CreateWord(\"%s\", OP_%s, %s);\n", $3, $2, flags
}
