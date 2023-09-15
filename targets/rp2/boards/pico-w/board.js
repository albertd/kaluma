// initialize board object
global.board.name = "pico-w";
global.__ieee80211dev = process.binding(process.binding.wifi);
global.__netdev = process.binding(process.binding.net);

// mount lfs on "/"
const fs = require("fs");
const { VFSLittleFS } = require("vfs_lfs");
const { Flash } = require("flash");
fs.register("lfs", VFSLittleFS);
// fs block starts after 4(storage) + 128(program)
const bd = new Flash(132, 128);
fs.mount("/", bd, "lfs", true);
