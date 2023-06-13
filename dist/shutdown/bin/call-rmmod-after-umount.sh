#!/bin/bash

# Calls rmmod after filesystem is unmounted.

args=$1
umount.real $args
rmmod dattobd