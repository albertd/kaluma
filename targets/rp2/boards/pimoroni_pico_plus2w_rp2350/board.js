// initialize board object
global.board.name = "pimoroni_pico_plus2w_rp2350";

// mount lfs on "/"
const fs = require("fs");
const { VFSLittleFS } = require("vfs_lfs");
const { Flash } = require("flash");
fs.register("lfs", VFSLittleFS);
// fs block starts after 4(storage) + 128(program)
const bd = new Flash(132, 3625);
fs.mount("/", bd, "lfs", true);

const { PicoCYW43, PicoCYW43WIFI, PicoCYW43Network } = require("pico_cyw43");
global.__ieee80211dev = new PicoCYW43WIFI();
global.__netdev = new PicoCYW43Network();

const picoCYW43 = new PicoCYW43();
picoCYW43.putGpio(0, true);
