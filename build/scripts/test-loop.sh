#!/bin/bash
set -e
TEST_IMG="/tmp/test-loop.img"
truncate -s 10M "$TEST_IMG"
echo "Testing losetup with $TEST_IMG"
LOOP="$(losetup --find --show "$TEST_IMG")"
echo "Attached to $LOOP"
losetup -d "$LOOP"
rm "$TEST_IMG"
echo "Success"
