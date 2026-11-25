#!/bin/bash
# Finds a hanging or failing Catch2 case: runs the unit binary (add -s as an extra argument to see every assertion) under a time limit and
# prints the tail. Usage: run_tests_limited.sh <path to forever_lua_tests> [seconds] [extra catch args]
BIN=$1; LIMIT=${2:-120}; shift 2
timeout "$LIMIT" "$BIN" "$@" 2>&1 | tail -150
echo "exit=${PIPESTATUS[0]}"
