#!/bin/bash
#
# Search files/dirs for commits.
# Not case sensitive.

ABORT() {
	echo "Error: $*"
	exit 1
}

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Searches files/dirs for commit headlines."
  echo "Usage: ./search_git.sh <search string> <path>"
  exit 1
fi

[ "$1" ] || ABORT "Try --help | -h"
[ "$2" ] || ABORT "Provide search path."

git log --oneline "$2" | grep --color=auto -i "$1"
