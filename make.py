#!/usr/bin/env python3

import licant
import licant.install
import os

licant.install.install_library(
	tgt="install",
	uninstall="uninstall",
	hroot="subprocess",
	headers="include")

licant.ex("install")
